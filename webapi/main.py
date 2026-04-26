import socket
import logging
from fastapi import FastAPI, HTTPException, Request, Depends
from fastapi.responses import RedirectResponse
from pydantic import BaseModel, EmailStr
from sqlalchemy import create_engine, Column, Integer, String, DateTime
from sqlalchemy.ext.declarative import declarative_base
from sqlalchemy.orm import sessionmaker, Session
import datetime
import os
from authlib.integrations.starlette_client import OAuth
from starlette.middleware.sessions import SessionMiddleware
from dotenv import load_dotenv

# Load env vars
load_dotenv()

# Configuration
M59_SERVER_IP = "138.197.44.253"
M59_MAINTENANCE_PORT = 9998
DATABASE_URL = "mysql+pymysql://root:m59_secret_pass@localhost/m59_web"
SECRET_KEY = os.getenv("SECRET_KEY", "a_very_secret_key_change_me")
GOOGLE_CLIENT_ID = os.getenv("GOOGLE_CLIENT_ID")
GOOGLE_CLIENT_SECRET = os.getenv("GOOGLE_CLIENT_SECRET")

# Logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

app = FastAPI(title="Meridian 59 Account API")
app.add_middleware(SessionMiddleware, secret_key=SECRET_KEY)

# OAuth Setup
oauth = OAuth()
oauth.register(
    name='google',
    client_id=GOOGLE_CLIENT_ID,
    client_secret=GOOGLE_CLIENT_SECRET,
    server_metadata_url='https://accounts.google.com/.well-known/openid-configuration',
    client_kwargs={
        'scope': 'openid email profile'
    }
)

# Database Setup
Base = declarative_base()

class WebUser(Base):
    __tablename__ = "web_users"
    id = Column(Integer, primary_key=True, index=True)
    google_id = Column(String(100), unique=True, index=True)
    email = Column(String(100), unique=True, index=True)
    username = Column(String(50), nullable=True)
    m59_account_id = Column(Integer, nullable=True)
    created_at = Column(DateTime, default=datetime.datetime.utcnow)

engine = create_engine(DATABASE_URL)
SessionLocal = sessionmaker(autocommit=False, autoflush=False, bind=engine)

class AccountCreate(BaseModel):
    username: str
    password: str
    account_type: str = "user" # user, admin, dm

def send_m59_command(command: str):
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(5)
            s.connect((M59_SERVER_IP, M59_MAINTENANCE_PORT))
            greeting = s.recv(1024).decode()
            s.sendall(f"{command}\r\n".encode())
            response = s.recv(4096).decode()
            s.sendall(b"QUIT\r\n")
            return response
    except Exception as e:
        logger.error(f"Error communicating with M59 server: {e}")
        raise HTTPException(status_code=500, detail=f"M59 Server Error: {e}")

@app.get("/login")
async def login(request: Request):
    redirect_uri = request.url_for('auth')
    return await oauth.google.authorize_redirect(request, str(redirect_uri))

@app.get("/auth")
async def auth(request: Request):
    try:
        token = await oauth.google.authorize_access_token(request)
    except Exception as e:
        logger.error(f"OAuth Error: {e}")
        raise HTTPException(status_code=400, detail="OAuth authentication failed")
    
    user_info = token.get('userinfo')
    if user_info:
        request.session['user'] = dict(user_info)
    return RedirectResponse(url='/')

from fastapi.responses import RedirectResponse, HTMLResponse

...

@app.get("/user/me")
def get_me(request: Request):
    user = request.session.get('user')
    if not user:
        return {"authenticated": False}
    return {"authenticated": True, "user": user}

@app.get("/", response_class=HTMLResponse)
def read_root(request: Request):
    return """
    <!DOCTYPE html>
    <html lang="en">
    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title>Meridian 59 Identity Portal</title>
        <script src="https://unpkg.com/vue@3/dist/vue.global.js"></script>
        <script src="https://cdn.tailwindcss.com"></script>
        <style>
            body { background-color: #0c0c0c; color: #d4af37; font-family: 'Georgia', serif; }
            .m59-card { background-color: #1a1a1a; border: 2px solid #d4af37; box-shadow: 0 0 15px rgba(212, 175, 55, 0.2); }
            .m59-button { background-color: #d4af37; color: #1a1a1a; font-weight: bold; transition: all 0.3s; }
            .m59-button:hover { background-color: #f1c40f; transform: scale(1.05); }
            .m59-input { background-color: #2a2a2a; border: 1px solid #d4af37; color: #fff; }
        </style>
    </head>
    <body class="flex items-center justify-center min-h-screen">
        <div id="app" class="w-full max-w-md p-8 m59-card rounded-lg text-center">
            <h1 class="text-3xl mb-6 uppercase tracking-widest">Meridian 59</h1>
            <h2 class="text-xl mb-8 text-gray-400">Account Management</h2>

            <div v-if="loading" class="animate-pulse">Loading the realm...</div>

            <div v-else>
                <!-- Login State -->
                <div v-if="!user.authenticated">
                    <p class="mb-8 text-gray-300">Welcome, Traveler. To manage your accounts, you must first verify your identity.</p>
                    <a href="/login" class="inline-block px-8 py-3 m59-button rounded uppercase tracking-wider">
                        Login with Google
                    </a>
                </div>

                <!-- Dashboard State -->
                <div v-else>
                    <div class="mb-6 p-4 bg-black rounded-lg border border-gray-800">
                        <p class="text-sm text-gray-500 uppercase">Verified Identity</p>
                        <p class="text-white font-mono">{{ user.user.email }}</p>
                    </div>

                    <div v-if="!successMessage">
                        <p class="mb-4 text-left text-sm">Provision a new game account:</p>
                        <form @submit.prevent="createAccount" class="space-y-4">
                            <input v-model="form.username" type="text" placeholder="Character Name" class="w-full p-2 m59-input rounded" required>
                            <input v-model="form.password" type="password" placeholder="Password" class="w-full p-2 m59-input rounded" required>
                            <button type="submit" :disabled="creating" class="w-full py-3 m59-button rounded uppercase">
                                {{ creating ? 'Communicating with Server...' : 'Create Account' }}
                            </button>
                        </form>
                    </div>

                    <div v-else class="mt-6 p-4 bg-green-900 border border-green-500 text-white rounded">
                        <p class="font-bold">✨ {{ successMessage }}</p>
                        <p class="mt-2 text-sm text-green-200">You can now login using the Meridian 59 TUI or 3D client.</p>
                    </div>

                    <div v-if="error" class="mt-4 p-2 bg-red-900 border border-red-500 text-red-100 text-sm rounded">
                        {{ error }}
                    </div>

                    <a href="/login" class="mt-8 block text-xs text-gray-500 hover:text-gray-300 underline">Switch Account</a>
                </div>
            </div>
            
            <p class="mt-12 text-[10px] text-gray-600 uppercase tracking-tighter">Powered by Meridian 59 Server 900</p>
        </div>

        <script>
            const { createApp, ref, onMounted } = Vue
            createApp({
                setup() {
                    const user = ref({ authenticated: false })
                    const loading = ref(true)
                    const creating = ref(false)
                    const error = ref(null)
                    const successMessage = ref(null)
                    const form = ref({ username: '', password: '' })

                    const fetchUser = async () => {
                        try {
                            const res = await fetch('/user/me')
                            user.value = await res.json()
                        } finally {
                            loading.value = false
                        }
                    }

                    const createAccount = async () => {
                        creating.value = true
                        error.value = null
                        try {
                            const res = await fetch('/accounts/create', {
                                method: 'POST',
                                headers: { 'Content-Type': 'application/json' },
                                body: JSON.stringify(form.value)
                            })
                            const data = await res.json()
                            if (res.ok && data.message === "Account created successfully") {
                                successMessage.value = data.message
                            } else {
                                error.value = data.detail || data.raw_response || "Failed to create account"
                            }
                        } catch (e) {
                            error.value = "Network error communicating with portal"
                        } finally {
                            creating.value = false
                        }
                    }

                    onMounted(fetchUser)
                    return { user, loading, creating, error, successMessage, form, createAccount }
                }
            }).mount('#app')
        </script>
    </body>
    </html>
    """


@app.post("/accounts/create")
async def create_account(request: Request, account: AccountCreate):
    # 1. Require Google OIDC Authentication
    user = request.session.get('user')
    if not user:
        raise HTTPException(status_code=401, detail="Authentication required via Google OIDC")
    
    # 2. Issue command to M59 server using the verified email from Google
    email = user['email']
    cmd = f"account create {account.account_type} {account.username} {account.password} {email}"
    response = send_m59_command(cmd)
    
    if "Created account" in response or "Success" in response or "Changing name" in response:
        return {
            "message": "Account created successfully", 
            "google_verified_email": email,
            "raw_response": response.strip()
        }
    else:
        return {"message": "Server responded but creation might have failed", "raw_response": response.strip()}

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=80)
