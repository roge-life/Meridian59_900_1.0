from sqlalchemy import Column, Integer, String, DateTime, Boolean, UniqueConstraint
from sqlalchemy.ext.declarative import declarative_base
import datetime

Base = declarative_base()

class WebUser(Base):
    __tablename__ = "web_users"

    id         = Column(Integer, primary_key=True, index=True)
    google_id  = Column(String(100), unique=True, index=True, nullable=True)
    discord_id = Column(String(100), unique=True, index=True, nullable=True)
    email      = Column(String(100), unique=True, index=True)
    is_admin   = Column(Boolean, default=False)
    m59_account_id = Column(Integer, nullable=True)  # legacy stub
    created_at = Column(DateTime, default=datetime.datetime.utcnow)
    last_login = Column(DateTime, default=datetime.datetime.utcnow)

class M59Account(Base):
    __tablename__ = "m59_accounts"
    __table_args__ = (UniqueConstraint('web_user_id', 'slot', name='uq_user_slot'),)

    id          = Column(Integer, primary_key=True)
    web_user_id = Column(Integer, index=True, nullable=False)
    slot        = Column(Integer, nullable=False)
    username    = Column(String(200), index=True, nullable=False)
    updated_at  = Column(DateTime, default=datetime.datetime.utcnow, onupdate=datetime.datetime.utcnow)
