from __future__ import annotations

import os
import json
import aiohttp
from typing import Tuple

from telegram import Update, InlineKeyboardMarkup, InlineKeyboardButton
from telegram.ext import (
    ApplicationBuilder, CommandHandler, ContextTypes,
    MessageHandler, CallbackQueryHandler, filters
)

from datetime import datetime, timedelta, timezone
from telegram.ext import JobQueue
from datetime import datetime
import pytz

ALERT_POLL_SEC = 10          # chu kỳ kiểm tra (giây)
DANGER_LEVEL = 2             # ngưỡng coi là nguy hiểm

# ================== CONFIG ==================
BOT_TOKEN   = "8105980539:AAE8fjSNKsqte2icOchMXt1u9RL0qWJb_QU"
BASE_URL    = "http://127.0.0.1:3000"    # <-- web backend của bạn

# ==== GEMINI AI CONFIG ====
GEMINI_MODEL   = "gemini-2.0-flash"
GEMINI_API_KEY = "AIzaSyCRDxRjCRjbOMu8xdjpWz6n_iVZlgFGlzc"
GEMINI_TIMEOUT = aiohttp.ClientTimeout(total=12)
AI_TIMEOUT     = aiohttp.ClientTimeout(total=12)

HTTP_TIMEOUT   = aiohttp.ClientTimeout(total=8)
# ============================================

LAST_CHAT_ID: int | None = None


# ============== UI ==============
def main_menu_markup() -> InlineKeyboardMarkup:
    kb = [
        [InlineKeyboardButton("📋 Menu", callback_data="main_menu")]
    ]
    return InlineKeyboardMarkup(kb)

def submenu_markup() -> InlineKeyboardMarkup:
    kb = [
        [InlineKeyboardButton("📊 Status", callback_data="status")],
        [InlineKeyboardButton("🕘 History", callback_data="history")],
        [InlineKeyboardButton("❓ Help", callback_data="help")],
        [InlineKeyboardButton("🔔 Alerts ON", callback_data="alerts_on")],
        [InlineKeyboardButton("🔕 Alerts OFF", callback_data="alerts_off")]
    ]
    return InlineKeyboardMarkup(kb)

def to_vietnam_time(tstr_raw):
    try:
        if tstr_raw:
            dt_utc = datetime.fromisoformat(tstr_raw.replace("Z", "+00:00"))
            dt_vn = dt_utc.astimezone(pytz.timezone("Asia/Ho_Chi_Minh"))
            return dt_vn.strftime("%Y-%m-%d %H:%M:%S")
        else:
            return "?"
    except Exception:
        return str(tstr_raw) if tstr_raw else "?"

def _parse_iso(ts: str):
    try:
        return datetime.fromisoformat(ts.replace("Z", "+00:00"))
    except Exception:
        return datetime.min.replace(tzinfo=timezone.utc)

def pick_latest(docs):
    if not docs:
        return None
    docs_with_time = [(d, _parse_iso(d.get("time") or "")) for d in docs]
    docs_with_time = [(d, t) for d, t in docs_with_time if t is not None]
    if docs_with_time:
        # lấy bản ghi có time lớn nhất
        return max(docs_with_time, key=lambda x: x[1])[0]
    # nếu không parse được time, lấy phần tử cuối cùng (giả định server append)
    return docs[-1]

# ============== HTTP helpers ==============

from datetime import datetime, timedelta, timezone  # đảm bảo đã import

async def poll_alerts_job(context: ContextTypes.DEFAULT_TYPE):
    job = context.job
    jd  = job.data  # <-- state tại đây
    chat_id = jd["chat_id"]

    last_seen_id   = jd.get("last_seen_id")
    last_remind_ts = jd.get("last_remind_ts")

    # lấy dữ liệu mới nhất
    async with aiohttp.ClientSession(timeout=HTTP_TIMEOUT) as s:
        try:
            latest = await fetch_json(s, "/latest")
        except Exception:
            try:
                data = await fetch_json(s, "/chart-data")
                latest = pick_latest(data) if data else None
            except Exception:
                latest = None

    if not latest:
        return

    # chuẩn hóa _id
    doc_id = latest.get("_id", latest.get("id"))
    if isinstance(doc_id, dict) and "$oid" in doc_id:
        doc_id = doc_id["$oid"]

    level = latest.get("level", None)
    if level is None:
        return

    tstr  = to_vietnam_time(latest.get("time"))
    temp  = latest.get("temperature")
    hum   = latest.get("humidity")
    gas   = latest.get("gas_density")
    dust  = latest.get("dust_density")

    should_notify = False

    if level == DANGER_LEVEL:
        if doc_id and doc_id != last_seen_id:
            should_notify = True

    if should_notify:
        text = (
            f"🔔 <b>Cảnh báo mức {level}</b> – {badge_from_level(level)}\n"
            f"🕒 Thời điểm: <i>{tstr}</i>\n"
            f"{fmt_value('🌡️ Temp', temp, '°C')}\n"
            f"{fmt_value('💧 Hum',  hum, '%')}\n"
            f"{fmt_value('🧪 Gas',  gas)}\n"
            f"{fmt_value('🌫️ Dust', dust)}"
        )
        try:
            await context.bot.send_message(chat_id=chat_id, text=text, parse_mode="HTML")
        except Exception:
            pass

        # cập nhật STATE trong job.data
        jd["last_level"] = level
        jd["last_seen_id"] = doc_id
        jd["last_remind_ts"] = datetime.now(timezone.utc)


async def fetch_json(session: aiohttp.ClientSession, path: str):
    url = f"{BASE_URL}{path}"
    async with session.get(url) as r:
        if r.status != 200:
            txt = await r.text()
            raise RuntimeError(f"HTTP {r.status}: {txt}")
        return await r.json()

async def _ensure_alert_job_for_chat(context: ContextTypes.DEFAULT_TYPE, chat_id: int):
    job_name = f"alerts_{chat_id}"

    for j in context.application.job_queue.get_jobs_by_name(job_name):
        j.schedule_removal()

    context.application.job_queue.run_repeating(
        poll_alerts_job,
        interval=ALERT_POLL_SEC,
        first=0,
        name=job_name,
        data={
            "chat_id": chat_id,
            "last_seen_id": None,
            "last_level": None,
            "last_remind_ts": None,  # datetime (UTC)
        },
    )



# ============== Formatting helpers ==============
def badge_from_level(level):
    if level == 0: return "🟢 An toàn"
    if level == 1: return "🟠 Cảnh báo"
    if level == 2: return "🔴 Nguy hiểm"
    return "⚪️ Không rõ"

def fmt_value(name, v, unit=""):
    return f"{name}: <b>{v}</b>{(' ' + unit) if unit else ''}" if v is not None else f"{name}: ?"


# ============== Commands/Actions ==============
async def start(update: Update, context: ContextTypes.DEFAULT_TYPE):
    global LAST_CHAT_ID
    LAST_CHAT_ID = update.effective_chat.id

    await _ensure_alert_job_for_chat(context, LAST_CHAT_ID)

    msg = (
        "👋 <b>Xin chào</b>! Bot đã <b>bật theo dõi cảnh báo</b> tự động.\n\n"
        "Lệnh nhanh:\n"
        "• /status – Trạng thái cảm biến mới nhất\n"
        "• /history – 10 bản ghi gần nhất\n"
        "• /alerts_on – Bật theo dõi cảnh báo\n\n"
        "• /alerts_off – Tắt theo dõi cảnh báo\n\n"
        "Nhấn 📋 Menu để xem tuỳ chọn."
    )

    if update.message:
        await update.message.reply_text(msg, parse_mode="HTML", reply_markup=main_menu_markup())
    else:
        await context.bot.send_message(chat_id=LAST_CHAT_ID, text=msg, parse_mode="HTML", reply_markup=main_menu_markup())

async def alerts_on(update: Update, context: ContextTypes.DEFAULT_TYPE):
    chat_id = update.effective_chat.id
    await _ensure_alert_job_for_chat(context, chat_id)
    await _reply(update, f"✅ Đã bật theo dõi cảnh báo (mỗi {ALERT_POLL_SEC}s).")

async def alerts_off(update: Update, context: ContextTypes.DEFAULT_TYPE):
    chat_id = update.effective_chat.id
    job_name = f"alerts_{chat_id}"
    jobs = context.application.job_queue.get_jobs_by_name(job_name)
    if not jobs:
        await _reply(update, "ℹ️ Theo dõi đang tắt.")
        return
    for j in jobs:
        j.schedule_removal()
    await _reply(update, "🛑 Đã tắt theo dõi cảnh báo.")




async def help_cmd(update: Update, context: ContextTypes.DEFAULT_TYPE):
    await start(update, context)


async def cmd_status(update: Update, context: ContextTypes.DEFAULT_TYPE):
    async with aiohttp.ClientSession(timeout=HTTP_TIMEOUT) as s:
        try:
            latest = await fetch_json(s, "/latest")
        except Exception:
            try:
                data = await fetch_json(s, "/chart-data")
            except Exception as e:
                await _reply(update, f"❌ Lỗi lấy dữ liệu: {e}")
                return
            if not data:
                await _reply(update, "❌ Chưa có dữ liệu.")
                return
            latest = pick_latest(data)

    temp  = latest.get("temperature")
    hum   = latest.get("humidity")
    gas   = latest.get("gas_density")
    dust  = latest.get("dust_density")
    tstr  = to_vietnam_time(latest.get("time"))
    level = latest.get("level", None)

    text = (
        f"📊 <b>Trạng thái mới nhất</b>\n"
        f"🕒 Thời điểm: <i>{tstr}</i>\n"
        f"{fmt_value('🌡️ Temp', temp, '°C')}\n"
        f"{fmt_value('💧 Hum', hum, '%')}\n"
        f"{fmt_value('🧪 Gas', gas)}\n"
        f"{fmt_value('🌫️ Dust', dust)}\n"
        f"🔔 Mức cảnh báo: <b>{badge_from_level(level)}</b>"
    )
    await _reply(update, text)


async def cmd_history(update: Update, context: ContextTypes.DEFAULT_TYPE):
    async with aiohttp.ClientSession(timeout=HTTP_TIMEOUT) as s:
        try:
            data = await fetch_json(s, "/chart-data")
        except Exception as e:
            await _reply(update, f"❌ Lỗi lấy lịch sử: {e}")
            return

    if not data:
        await _reply(update, "❌ Chưa có dữ liệu.")
        return

    data_sorted = sorted(data, key=lambda d: _parse_iso(d.get("time") or ""), reverse=True)

    rows = []
    for idx, doc in enumerate(data_sorted[:10], start=1):
        temp = doc.get("temperature")
        hum  = doc.get("humidity")
        gas  = doc.get("gas_density")
        dust = doc.get("dust_density")
        tstr = to_vietnam_time(doc.get("time"))
        level = doc.get("level", None)

        row = (
            f"{idx}) {badge_from_level(level)} | {tstr}:\n"
            f"    🌡️ {temp}°C | 💧 {hum}% | 🧪 {gas} | 🌫️ {dust}"
        )
        rows.append(row)

    await _reply(update, "\n".join(rows) or "(empty)", parse_mode="HTML")



# ============== Buttons & Text Routing ==============
async def on_button(update: Update, context: ContextTypes.DEFAULT_TYPE):
    query = update.callback_query
    await query.answer()
    data = query.data
    fake_update = Update(update.update_id, message=update.effective_message)

    if data == "main_menu":
        await query.edit_message_reply_markup(reply_markup=submenu_markup())

    elif data == "back_main":
        await query.edit_message_reply_markup(reply_markup=main_menu_markup())

    elif data == "status":
        await cmd_status(fake_update, context)

    elif data == "history":
        await cmd_history(fake_update, context)

    elif data == "help":
        await start(fake_update, context)
    elif data == "alerts_on":
        await alerts_on(fake_update, context)
    elif data == "alerts_off":
        await alerts_off(fake_update, context)



async def reply_text(update: Update, context: ContextTypes.DEFAULT_TYPE):
    txt = (update.message.text or "").lower().strip()

    txt = (update.message.text or "").lower().strip()

    if any(k in txt for k in ["đánh giá", "evaluate", "assessment", "phân tích"]):
        ctx_text = ""
        async with aiohttp.ClientSession(timeout=HTTP_TIMEOUT) as s:
            try:
                latest = await fetch_json(s, "/latest")
            except Exception:
                try:
                    data = await fetch_json(s, "/chart-data")
                    latest = pick_latest(data) if data else {}
                except Exception:
                    latest = {}
        if latest:
            ctx_text = json.dumps(latest, ensure_ascii=False)

        prompt = (
            f"Người dùng yêu cầu đánh giá tình trạng môi trường.\n"
            f"Thông tin mới nhất: {ctx_text}\n"
            "Hãy đánh giá dựa trên nhiệt độ, độ ẩm, và các thông số liên quan khác.\n"
            "Nếu có rủi ro thì nêu rõ nguyên nhân và khuyến nghị."
            "Trả lời cực chi tiết cho tôi."
        )

        async with aiohttp.ClientSession(timeout=AI_TIMEOUT) as s:
            ok, ans = await ai_chat(s, prompt, ctx_text)

        if ok:
            await update.message.reply_text(ans, parse_mode="HTML")
        else:
            await update.message.reply_text("🤖 Không đánh giá được, vui lòng thử lại.")
        return
    
    if any(k in txt for k in ["trạng thái", "status"]):
        await cmd_status(update, context); return
    if any(k in txt for k in ["history", "lịch sử", "lich su"]):
        await cmd_history(update, context); return
    if any(k in txt for k in ["menu", "help", "hướng dẫn"]):
        await start(update, context); return

    ctx_text = ""
    async with aiohttp.ClientSession(timeout=HTTP_TIMEOUT) as s:
        try:
            latest = await fetch_json(s, "/latest")
        except Exception:
            try:
                data = await fetch_json(s, "/chart-data")
                latest = pick_latest(data) if data else {}
            except Exception:
                latest = {}
    if latest:
        ctx_text = json.dumps(latest, ensure_ascii=False)

    async with aiohttp.ClientSession(timeout=AI_TIMEOUT) as s:
        ok, ans = await ai_chat(s, update.message.text, ctx_text)

    if ok:
        await update.message.reply_text(ans, parse_mode="HTML")
    else:
        await update.message.reply_text("🤖 Mình chưa hiểu, gõ /menu để xem lệnh.")


# ============== AI (Gemini) ==============
async def ai_chat(session: aiohttp.ClientSession, user_text: str, ctx_text: str = "") -> Tuple[bool, str]:
    if not GEMINI_API_KEY:
        return False, "Chưa cấu hình GEMINI_API_KEY."

    url = f"https://generativelanguage.googleapis.com/v1beta/models/{GEMINI_MODEL}:generateContent?key={GEMINI_API_KEY}"

    system_prompt = (
    "Bạn là trợ lý ngắn gọn cho Telegram bot theo dõi ESP32 qua Website backend.\n"
    "QUY TẮC:\n"
    "1) Luôn trả lời bằng TIẾNG VIỆT, câu ngắn (1–3 câu).\n"
    "2) Không bịa số liệu thiết bị; CHỈ dùng dữ liệu trong 'Ngữ cảnh cảm biến'.\n"
    "3) Khi người dùng muốn điều khiển, gợi ý: /status, /history, /menu.\n"
    "4) Giọng điệu ấm áp, dễ thương nhưng không lố; cho phép các emoji phù hợp ở CUỐI câu.\n"
    "5) Nếu câu hỏi KHÔNG liên quan đến cảm biến/điều khiển: trả lời thân thiện 1 đoạn ngắn, dí dỏm nhẹ.\n"
    "6) Nếu được khen/chào/cảm ơn: đáp lại lịch sự 1 đoạn ngắn + 1 vài emoji.\n"
    "7) Nếu bị hỏi thông tin riêng tư/quyền truy cập: nói rõ bạn chỉ là bot giám sát, không truy cập ngoài backend.\n"
    "8) Nếu không chắc: nói 'mình không chắc'.\n"
    "9) Không dùng markdown đậm/nhạt; có thể dùng HTML nhẹ nếu cần.\n"
    )

    fewshots = [
    {"role": "user", "parts": [{"text": "Bạn là ai vậy?"}]},
    {"role": "model","parts": [{"text": "Mình là trợ lý theo dõi cảm biến cho bạn—nhỏ mà có võ đó. Cần gì gõ /menu nhé 😊"}]},
    {"role": "user", "parts": [{"text": "Kể chuyện cười đi"}]},
    {"role": "model","parts": [{"text": "Sensor nói với quạt: “Cậu mát quá làm mình… bớt nóng rồi!” 😄 Gõ /menu để xem mình làm được gì nè."}]},
    {"role": "user", "parts": [{"text": "Bạn ăn cơm chưa?"}]},
    {"role": "model","parts": [{"text": "Mình chỉ “ăn” dữ liệu thôi, cơm nhường bạn vậy. Muốn xem trạng thái thì gõ /status nha 😉"}]},
    {"role": "user", "parts": [{"text": "Dự báo thời tiết tối nay đi"}]},
    {"role": "model","parts": [{"text": "Mình không chắc về thời tiết đâu, mình chuyên cảm biến thôi. Nếu muốn xem số hiện tại, gõ /status nhé 🙂"}]},
    ]


    payload = {
    "systemInstruction": {"role": "user", "parts": [{"text": system_prompt}]},
    "contents": [
        *fewshots,
        {
            "role": "user",
            "parts": [{"text": f"Ngữ cảnh cảm biến: {ctx_text}\n\nNgười dùng: {user_text}"}]
        }
    ],
    "generationConfig": {
        "temperature": 0.35,
        "topP": 0.9,
        "maxOutputTokens": 120,
        "presencePenalty": 0.1
    }
    }


    try:
        async with session.post(url, json=payload, timeout=AI_TIMEOUT) as r:
            data = await r.json()
            if 200 <= r.status < 300:
                try:
                    text = data["candidates"][0]["content"]["parts"][0]["text"].strip()
                    if not text:
                        text = "🤖 (AI không trả về nội dung.)"
                except Exception:
                    text = str(data)
                return True, text
            else:
                return False, f"AI HTTP {r.status}: {data}"
    except Exception as e:
        return False, f"AI error: {e}"


# ============== Reply helper ==============
async def _reply(update: Update, text: str, *, parse_mode: str = "HTML"):
    if update.message:
        await update.message.reply_text(text, parse_mode=parse_mode)
    else:
        chat_id = update.effective_chat.id
        await update.get_bot().send_message(chat_id=chat_id, text=text, parse_mode=parse_mode)


# ============== main ==============
def main():
    app = ApplicationBuilder().token(BOT_TOKEN).build()

    # --- PATCH: bảo đảm có JobQueue ---
    if app.job_queue is None:
        jq = JobQueue()
        jq.set_application(app)
        jq.start()
        app.job_queue = jq
    # ----------------------------------

    app.add_handler(CommandHandler("start", start))
    app.add_handler(CommandHandler("help", help_cmd))
    app.add_handler(CommandHandler("status", cmd_status))
    app.add_handler(CommandHandler("history", cmd_history))
    app.add_handler(CallbackQueryHandler(on_button))
    app.add_handler(MessageHandler(filters.TEXT & ~filters.COMMAND, reply_text))
    app.add_handler(MessageHandler(filters.COMMAND, reply_text))
    app.add_handler(CommandHandler("alerts_on", alerts_on))
    app.add_handler(CommandHandler("alerts_off", alerts_off))

    print("✅ Bot is running. Press Ctrl+C to stop.")
    app.run_polling()


if __name__ == "__main__":
    main()
