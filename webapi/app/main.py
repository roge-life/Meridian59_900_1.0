import socket
import logging
import os
import datetime
from fastapi import FastAPI, HTTPException, Request, Depends
from fastapi.responses import RedirectResponse, HTMLResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, EmailStr
from sqlalchemy import create_engine, text
from sqlalchemy.orm import sessionmaker, Session
from authlib.integrations.starlette_client import OAuth
from starlette.middleware.sessions import SessionMiddleware
from dotenv import load_dotenv

from .db.models import Base, WebUser, M59Account
from .core.m59 import M59Client

# Load env vars
load_dotenv()

# Configuration
M59_SERVER_IP = os.getenv("M59_SERVER_IP", "138.197.44.253")
M59_MAINTENANCE_PORT = int(os.getenv("M59_MAINTENANCE_PORT", 9998))
DATABASE_URL = os.getenv("DATABASE_URL", "mysql+pymysql://root:m59_secret_pass@localhost/m59_web")
SECRET_KEY = os.getenv("SECRET_KEY", "m59_secret_key_12345")
GOOGLE_CLIENT_ID = os.getenv("GOOGLE_CLIENT_ID")
GOOGLE_CLIENT_SECRET = os.getenv("GOOGLE_CLIENT_SECRET")
BLAKSERV_DATABASE_URL = os.getenv("BLAKSERV_DATABASE_URL", "mysql+pymysql://m59reader:m59read_pass@localhost/blakserv")

ADMIN_EMAILS = frozenset({"joel.palmtag@gmail.com", "markpalmtag@gmail.com"})

# Logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

app = FastAPI(title="Meridian 59 Identity Portal")
app.add_middleware(SessionMiddleware, secret_key=SECRET_KEY)

# Serve static files
static_dir = os.path.join(os.path.dirname(__file__), "..", "static")
app.mount("/static", StaticFiles(directory=static_dir), name="static")

# M59 Protocol Client
m59 = M59Client(M59_SERVER_IP, M59_MAINTENANCE_PORT)

# OAuth Setup
oauth = OAuth()
oauth.register(
    name='google',
    client_id=GOOGLE_CLIENT_ID,
    client_secret=GOOGLE_CLIENT_SECRET,
    server_metadata_url='https://accounts.google.com/.well-known/openid-configuration',
    client_kwargs={'scope': 'openid email profile'}
)

# Database — web portal
engine = create_engine(DATABASE_URL)
SessionLocal = sessionmaker(autocommit=False, autoflush=False, bind=engine)

def get_db():
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()

# Database — blakserv analytics (read-only)
blakserv_engine = create_engine(BLAKSERV_DATABASE_URL)
BlakservSession = sessionmaker(autocommit=False, autoflush=False, bind=blakserv_engine)

def get_blakserv_db():
    db = BlakservSession()
    try:
        yield db
    finally:
        db.close()

def require_admin(request: Request):
    user = request.session.get('user')
    if not user or user.get('email') not in ADMIN_EMAILS:
        raise HTTPException(status_code=403, detail="Forbidden")
    return user

MAX_ACCOUNTS_PER_USER = 5

def slot_username(email: str, slot: int) -> str:
    return email if slot == 1 else f"{email}.{slot}"

# DTOs
class AccountCreate(BaseModel):
    password: str
    account_type: str = "user"

class PasswordReset(BaseModel):
    password: str
    slot: int = 1

class ConsoleCmd(BaseModel):
    command: str

class AdminAccountCreate(BaseModel):
    name: str
    password: str
    email: str
    account_type: str = "user"  # user | admin | dm

@app.on_event("startup")
def startup():
    Base.metadata.create_all(bind=engine)

@app.get("/login")
async def login(request: Request):
    redirect_uri = request.url_for('auth')
    # Support HTTPS behind proxy
    if os.getenv("FORCE_HTTPS"):
        redirect_uri = str(redirect_uri).replace("http://", "https://")
    return await oauth.google.authorize_redirect(request, str(redirect_uri))

@app.get("/auth")
async def auth(request: Request, db: Session = Depends(get_db)):
    try:
        token = await oauth.google.authorize_access_token(request)
    except Exception as e:
        logger.error(f"OAuth Error: {e}")
        raise HTTPException(status_code=400, detail="OAuth authentication failed")
    
    user_info = token.get('userinfo')
    if user_info:
        email = user_info['email']
        # Upsert WebUser
        web_user = db.query(WebUser).filter(WebUser.email == email).first()
        if not web_user:
            web_user = WebUser(
                email=email,
                google_id=user_info.get('sub'),
                is_admin=False # Default to False, manually set in DB for now
            )
            db.add(web_user)
        else:
            web_user.last_login = datetime.datetime.utcnow()
        
        db.commit()
        request.session['user'] = {
            "email": web_user.email,
            "is_admin": web_user.is_admin,
            "id": web_user.id
        }
        
    return RedirectResponse(url='/')

@app.get("/user/me")
def get_me(request: Request):
    user = request.session.get('user')
    if not user:
        return {"authenticated": False}
    return {"authenticated": True, "user": user}

@app.get("/accounts/me")
async def check_my_account(request: Request, db: Session = Depends(get_db)):
    user = request.session.get('user')
    if not user:
        raise HTTPException(status_code=401, detail="Authentication required")

    slots = []
    for slot in range(1, MAX_ACCOUNTS_PER_USER + 1):
        username = slot_username(user['email'], slot)
        try:
            response = m59.get_user_info(username)
            exists = (
                response.strip()
                and "Cannot find account" not in response
                and "Cannot find user" not in response
                and username.lower() in response.lower()
            )
            if exists:
                row = db.query(M59Account).filter_by(web_user_id=user['id'], slot=slot).first()
                if row:
                    row.username   = username
                    row.updated_at = datetime.datetime.utcnow()
                else:
                    db.add(M59Account(web_user_id=user['id'], slot=slot, username=username))
                db.commit()
            slots.append({"slot": slot, "username": username, "exists": exists})
        except Exception as e:
            logger.error(f"Account check failed for slot {slot}: {e}")
            slots.append({"slot": slot, "username": username, "exists": False, "error": str(e)})

    return {"slots": slots}

@app.post("/accounts/create")
async def create_account(request: Request, account: AccountCreate, db: Session = Depends(get_db)):
    user = request.session.get('user')
    if not user:
        raise HTTPException(status_code=401, detail="Authentication required")

    # Find the first empty slot
    next_slot = None
    for slot in range(1, MAX_ACCOUNTS_PER_USER + 1):
        username = slot_username(user['email'], slot)
        try:
            response = m59.get_user_info(username)
            slot_taken = (
                response.strip()
                and "Cannot find account" not in response
                and "Cannot find user" not in response
                and username.lower() in response.lower()
            )
            if not slot_taken:
                next_slot = slot
                break
        except Exception as e:
            raise HTTPException(status_code=500, detail=str(e))

    if next_slot is None:
        raise HTTPException(status_code=400, detail=f"Account limit reached ({MAX_ACCOUNTS_PER_USER} accounts maximum)")

    username = slot_username(user['email'], next_slot)
    try:
        response = m59.create_account(username, account.password, user['email'], account.account_type)
        if "Created account" in response or "Success" in response or "Changing name" in response:
            return {"message": "Account created successfully", "username": username, "slot": next_slot, "raw_response": response.strip()}
        else:
            raise HTTPException(status_code=400, detail=f"M59 Error: {response.strip()}")
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/accounts/reset-password")
async def reset_password(request: Request, account: PasswordReset):
    user = request.session.get('user')
    if not user:
        raise HTTPException(status_code=401, detail="Authentication required")

    if account.slot < 1 or account.slot > MAX_ACCOUNTS_PER_USER:
        raise HTTPException(status_code=400, detail=f"Slot must be between 1 and {MAX_ACCOUNTS_PER_USER}")

    username = slot_username(user['email'], account.slot)
    try:
        response = m59.set_password(username, account.password)
        if "Set password" in response or "Success" in response:
            return {"message": "Password reset successfully", "username": username, "raw_response": response.strip()}
        else:
            raise HTTPException(status_code=400, detail=f"M59 Error: {response.strip()}")
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

# ── Admin panel ──────────────────────────────────────────────────────────────

@app.get("/admin", response_class=HTMLResponse)
def admin_panel(admin=Depends(require_admin)):
    path = os.path.join(os.path.dirname(__file__), "..", "static", "admin.html")
    with open(path) as f:
        return f.read()

@app.get("/admin/api/stats")
def admin_stats(
    admin=Depends(require_admin),
    bdb: Session = Depends(get_blakserv_db),
    db:  Session = Depends(get_db),
):
    today = datetime.datetime.utcnow().date()
    return {
        "total_players":  bdb.execute(text("SELECT COUNT(*) FROM player")).scalar(),
        "active_guilds":  bdb.execute(text("SELECT COUNT(*) FROM guild WHERE guild_disbanded=0")).scalar(),
        "logins_today":   bdb.execute(text("SELECT COUNT(*) FROM player_logins WHERE DATE(player_logins_time)=:d"), {"d": today}).scalar(),
        "deaths_today":   bdb.execute(text("SELECT COUNT(*) FROM player_death WHERE DATE(player_death_time)=:d"),  {"d": today}).scalar(),
        "web_users":      db.execute(text("SELECT COUNT(*) FROM web_users")).scalar(),
        "money_supply":   bdb.execute(text("SELECT player_money_total_amount FROM player_money_total ORDER BY player_money_total_time DESC LIMIT 1")).scalar() or 0,
    }

@app.get("/admin/api/players")
def admin_players(admin=Depends(require_admin), bdb: Session = Depends(get_blakserv_db)):
    rows = bdb.execute(text("SELECT * FROM player ORDER BY player_name")).fetchall()
    return [dict(r._mapping) for r in rows]

@app.get("/admin/api/logins")
def admin_logins(admin=Depends(require_admin), bdb: Session = Depends(get_blakserv_db)):
    rows = bdb.execute(text("SELECT * FROM player_logins ORDER BY player_logins_time DESC LIMIT 200")).fetchall()
    return [dict(r._mapping) for r in rows]

@app.get("/admin/api/deaths")
def admin_deaths(admin=Depends(require_admin), bdb: Session = Depends(get_blakserv_db)):
    rows = bdb.execute(text("SELECT * FROM player_death ORDER BY player_death_time DESC LIMIT 200")).fetchall()
    return [dict(r._mapping) for r in rows]

def _iso(v) -> str:
    if isinstance(v, datetime.datetime):
        return v.isoformat(sep='T', timespec='seconds')
    return str(v).replace(' ', 'T') if v else None

@app.get("/admin/api/economy")
def admin_economy(admin=Depends(require_admin), bdb: Session = Depends(get_blakserv_db)):
    supply  = bdb.execute(text("SELECT player_money_total_time, player_money_total_amount FROM player_money_total ORDER BY player_money_total_time DESC LIMIT 500")).fetchall()
    created = bdb.execute(text("SELECT money_created_time, money_created_amount FROM money_created ORDER BY money_created_time DESC LIMIT 500")).fetchall()
    return {
        "supply":  [{"t": _iso(r[0]), "v": int(r[1])} for r in supply],
        "created": [{"t": _iso(r[0]), "v": int(r[1])} for r in created],
    }

@app.get("/admin/api/guilds")
def admin_guilds(admin=Depends(require_admin), bdb: Session = Depends(get_blakserv_db)):
    rows = bdb.execute(text("SELECT * FROM guild ORDER BY guild_disbanded, guild_name")).fetchall()
    return [dict(r._mapping) for r in rows]

@app.get("/admin/api/web-users")
def admin_web_users(admin=Depends(require_admin), db: Session = Depends(get_db)):
    users = db.execute(text("SELECT id, email, google_id, is_admin, created_at, last_login FROM web_users ORDER BY created_at DESC")).fetchall()
    accounts = db.query(M59Account).all()
    accts_by_user = {}
    for a in accounts:
        accts_by_user.setdefault(a.web_user_id, []).append({"slot": a.slot, "username": a.username, "updated_at": str(a.updated_at)})
    result = []
    for u in users:
        row = dict(u._mapping)
        row["m59_accounts"] = sorted(accts_by_user.get(u.id, []), key=lambda x: x["slot"])
        result.append(row)
    return result

@app.post("/admin/api/console")
def admin_console(req: ConsoleCmd, admin=Depends(require_admin)):
    output = m59.send_command(req.command)
    return {"output": output}

@app.post("/admin/api/accounts/create")
def admin_create_account(req: AdminAccountCreate, admin=Depends(require_admin)):
    if req.account_type not in ("user", "admin", "dm"):
        raise HTTPException(status_code=400, detail="account_type must be user, admin, or dm")
    try:
        response = m59.create_account(req.name, req.password, req.email, req.account_type)
        if any(k in response for k in ("Created account", "Success", "Changing name")):
            return {"message": "Account created", "username": req.name, "raw_response": response.strip()}
        raise HTTPException(status_code=400, detail=f"M59 Error: {response.strip()}")
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))


@app.get("/admin/api/weekly-stats")
def admin_weekly_stats(admin=Depends(require_admin), bdb: Session = Depends(get_blakserv_db)):
    today = datetime.datetime.utcnow().date()
    labels, logins, deaths = [], [], []
    for i in range(6, -1, -1):
        day = today - datetime.timedelta(days=i)
        labels.append(day.strftime("%b %d"))
        logins.append(bdb.execute(
            text("SELECT COUNT(*) FROM player_logins WHERE DATE(player_logins_time)=:d"), {"d": day}).scalar())
        deaths.append(bdb.execute(
            text("SELECT COUNT(*) FROM player_death WHERE DATE(player_death_time)=:d"),  {"d": day}).scalar())
    return {"labels": labels, "logins": logins, "deaths": deaths}


# ── Server events ────────────────────────────────────────────────────────────

class FrenzyStart(BaseModel):
    duration: int = 60

@app.get("/admin/api/frenzy/status")
def admin_frenzy_status(admin=Depends(require_admin)):
    return m59.get_chaos_night()

@app.post("/admin/api/frenzy/start")
def admin_frenzy_start(req: FrenzyStart, admin=Depends(require_admin)):
    output = m59.start_chaos_night(duration=req.duration)
    return {"output": output}

@app.post("/admin/api/frenzy/end")
def admin_frenzy_end(admin=Depends(require_admin)):
    output = m59.end_chaos_night()
    return {"output": output}

@app.post("/admin/api/hall-of-heroes/update")
def admin_hoh_update(admin=Depends(require_admin)):
    output = m59.update_hall_of_heroes()
    return {"output": output}

@app.post("/admin/api/room-rental/recreate")
def admin_room_rental_recreate(admin=Depends(require_admin)):
    output = m59.recreate_room_rental()
    return {"output": output}

# ── Public ────────────────────────────────────────────────────────────────────

@app.get("/", response_class=HTMLResponse)
def read_root():
    static_path = os.path.join(os.path.dirname(__file__), "..", "static", "index.html")
    with open(static_path, "r") as f:
        return f.read()
