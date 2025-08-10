# pip install python-telegram-bot==20.7 aiohttp
from telegram import Update, InlineKeyboardMarkup, InlineKeyboardButton
from telegram.ext import ApplicationBuilder, CommandHandler, ContextTypes, MessageHandler, CallbackQueryHandler, filters
import asyncio
import aiohttp
import json
from typing import Tuple, Union
import os
# ================== CONFIG ==================
BOT_TOKEN   = "8105980539:AAE8fjSNKsqte2icOchMXt1u9RL0qWJb_QU"

AIO_USERNAME = "luuquang2005"  # <--- sửa
AIO_KEY      = "aio_Udfe24Z4Jur2XTDkGTy2QZRMBZ82"       # <--- sửa
AIO_BASE     = f"https://io.adafruit.com/api/v2/{AIO_USERNAME}"

# ==== GEMINI AI CONFIG ====
GEMINI_MODEL   = "gemini-2.0-flash"  # nhanh & rẻ; có free tier
GEMINI_API_KEY = "AIzaSyCRDxRjCRjbOMu8xdjpWz6n_iVZlgFGlzc"  # <-- dán key trực tiếp, hoặc để None để đọc ENV
GEMINI_TIMEOUT = aiohttp.ClientTimeout(total=12)
AI_TIMEOUT     = aiohttp.ClientTimeout(total=12)  # timeout riêng cho AI

FEED_TEMP = "nhiet-do"   # nhiệt độ
FEED_HUM  = "do-am"      # độ ẩm
FEED_GAS  = "gas"        # gas
FEED_DUST = "bui"        # bụi
FEED_CMD  = "cmd"        # feed lệnh (tạo thêm trên Adafruit IO)


HTTP_TIMEOUT = aiohttp.ClientTimeout(total=8)
# ============================================

# Lưu chat_id để có thể notify chủ động nếu cần
LAST_CHAT_ID: int | None = None

def main_menu_markup() -> InlineKeyboardMarkup:
    kb = [
        [InlineKeyboardButton("🔊 ON", callback_data="on"),
         InlineKeyboardButton("🔇 OFF", callback_data="off")],
        [InlineKeyboardButton("🧪 Test buzzer", callback_data="test"),
         InlineKeyboardButton("📊 Status", callback_data="status")],
        [InlineKeyboardButton("❓ Help", callback_data="help")]
    ]
    return InlineKeyboardMarkup(kb)

# ---------- Adafruit IO REST helpers ----------
async def aio_post(session: aiohttp.ClientSession, feed: str, value: str) -> Tuple[bool, str]:
    url = f"{AIO_BASE}/feeds/{feed}/data"
    headers = {"X-AIO-Key": AIO_KEY, "Content-Type": "application/json"}
    try:
        async with session.post(url, headers=headers, json={"value": value}) as r:
            txt = await r.text()
            return (200 <= r.status < 300), f"HTTP {r.status}: {txt}"
    except Exception as e:
        return False, f"REST error: {e}"

async def aio_latest(session: aiohttp.ClientSession, feed: str) -> Tuple[bool, Union[dict, str]]:
    url = f"{AIO_BASE}/feeds/{feed}/data/last"
    headers = {"X-AIO-Key": AIO_KEY}
    try:
        async with session.get(url, headers=headers) as r:
            txt = await r.text()
            if 200 <= r.status < 300:
                return True, json.loads(txt)
            return False, f"HTTP {r.status}: {txt}"
    except Exception as e:
        return False, f"REST error: {e}"

# ---------- Commands ----------
async def start(update: Update, context: ContextTypes.DEFAULT_TYPE):
    global LAST_CHAT_ID
    LAST_CHAT_ID = update.effective_chat.id
    msg = (
        "👋 <b>Xin chào</b>! Bot đang kết nối qua <b>Adafruit IO</b>.\n\n"
        "Lệnh:\n"
        "• /on – Bật (cmd)\n"
        "• /off – Tắt (cmd)\n"
        "• /test – Test buzzer (cmd)\n"
        "• /status – Đọc dữ liệu từ feeds\n\n"
        f"Feeds: <code>{FEED_TEMP}</code>, <code>{FEED_HUM}</code>, "
        f"<code>{FEED_GAS}</code>, <code>{FEED_DUST}</code>, cmd=<code>{FEED_CMD}</code>"
    )
    if update.message:
        await update.message.reply_text(msg, parse_mode="HTML", reply_markup=main_menu_markup())
    else:
        await context.bot.send_message(chat_id=LAST_CHAT_ID, text=msg, parse_mode="HTML", reply_markup=main_menu_markup())

async def help_cmd(update: Update, context: ContextTypes.DEFAULT_TYPE):
    await start(update, context)

async def cmd_on(update: Update, context: ContextTypes.DEFAULT_TYPE):
    async with aiohttp.ClientSession(timeout=HTTP_TIMEOUT) as s:
        ok, res = await aio_post(s, FEED_CMD, "on")
    await _reply(update, "✅ Đã gửi lệnh <b>ON</b>." if ok else f"❌ {res}")

async def cmd_off(update: Update, context: ContextTypes.DEFAULT_TYPE):
    async with aiohttp.ClientSession(timeout=HTTP_TIMEOUT) as s:
        ok, res = await aio_post(s, FEED_CMD, "off")
    await _reply(update, "✅ Đã gửi lệnh <b>OFF</b>." if ok else f"❌ {res}")

async def cmd_test(update: Update, context: ContextTypes.DEFAULT_TYPE):
    async with aiohttp.ClientSession(timeout=HTTP_TIMEOUT) as s:
        ok, res = await aio_post(s, FEED_CMD, "test")
    await _reply(update, "✅ Đã gửi lệnh <b>Test</b>." if ok else f"❌ {res}")

async def cmd_status(update: Update, context: ContextTypes.DEFAULT_TYPE):
    async with aiohttp.ClientSession(timeout=HTTP_TIMEOUT) as s:
        okT, t = await aio_latest(s, FEED_TEMP)
        okH, h = await aio_latest(s, FEED_HUM)
        okG, g = await aio_latest(s, FEED_GAS)
        okD, d = await aio_latest(s, FEED_DUST)

    if not (okT and okH and okG and okD):
        msg = "❌ Lỗi đọc feed:\n"
        for ok, name, val in [(okT,FEED_TEMP,t),(okH,FEED_HUM,h),(okG,FEED_GAS,g),(okD,FEED_DUST,d)]:
            if not ok: msg += f"- {name}: {val}\n"
        await _reply(update, msg)
        return

    def v(x):  # lấy value
        return (x.get("value") if isinstance(x, dict) else "?")

    pretty = (
        "📊 <b>ESP32 Status (Adafruit IO)</b>\n"
        f"🌡️ Temp: {v(t)} °C\n"
        f"💧 Hum : {v(h)} %\n"
        f"🧪 Gas : {v(g)}\n"
        f"🌫️ Dust: {v(d)}"
    )
    await _reply(update, pretty)

# ---------- Buttons & text ----------
async def on_button(update: Update, context: ContextTypes.DEFAULT_TYPE):
    query = update.callback_query
    await query.answer()
    # tạo fake update để tái dùng handlers
    fake_update = Update(update.update_id, message=update.effective_message)
    if query.data == "on":     await cmd_on(fake_update, context)
    elif query.data == "off":  await cmd_off(fake_update, context)
    elif query.data == "test": await cmd_test(fake_update, context)
    elif query.data == "status": await cmd_status(fake_update, context)
    elif query.data == "help":   await start(fake_update, context)

async def reply_text(update: Update, context: ContextTypes.DEFAULT_TYPE):
    txt = (update.message.text or "").lower().strip()
    if txt in ("on","bật","bật buzzer","mo buzzer"):               await cmd_on(update, context)
    elif txt in ("off","tắt","tắt buzzer","tat buzzer"):           await cmd_off(update, context)
    elif "test" in txt:                                            await cmd_test(update, context)
    elif "status" in txt or "trạng thái" in txt:                   await cmd_status(update, context)
    elif "menu" in txt or "help" in txt or "hướng dẫn" in txt:     await start(update, context)
    else:
        await update.message.reply_text("🤖 Gõ /menu để xem các nút lệnh.")

# ---------- small util ----------
async def _reply(update: Update, text: str, *, parse_mode: str = "HTML"):
    if update.message:
        await update.message.reply_text(text, parse_mode=parse_mode)
    else:
        # khi gọi từ callback button
        chat_id = update.effective_chat.id
        await update.get_bot().send_message(chat_id=chat_id, text=text, parse_mode=parse_mode)

# ---- GEMINI AI CALL (FIXED) ----
async def ai_chat(session: aiohttp.ClientSession, user_text: str, ctx_text: str = "") -> tuple[bool, str]:
    """
    Gọi Google Gemini để trả lời câu hỏi ngoài lề.
    Trả về (True, câu trả lời) hoặc (False, thông báo lỗi).
    """
    api_key = GEMINI_API_KEY or os.getenv("GEMINI_API_KEY")
    if not api_key:
        return False, "Chưa cấu hình GEMINI_API_KEY."

    url = f"https://generativelanguage.googleapis.com/v1beta/models/{GEMINI_MODEL}:generateContent?key={api_key}"

    system_prompt = (
    "Bạn là trợ lý ngắn gọn cho Telegram bot theo dõi ESP32 qua Adafruit IO. "
    "QUY TẮC: 1) Luôn trả lời bằng TIẾNG VIỆT."
    "2) Không tự bịa số liệu thiết bị"
    "3) Nếu người dùng muốn điều khiển, gợi ý /on, /off, /test, /status hoặc nút Menu. "
    "4) Câu hỏi ngoài lề: trả lời rất ngắn, lịch sự."
    )   

    FEW_SHOTS = [
        {"role": "user",  "parts": [{"text": "bật buzzer đi"}]},
        {"role": "model", "parts": [{"text": "Để bật còi, dùng /test hoặc nút “Buzzer”."}]},
        {"role": "user",  "parts": [{"text": "nhiệt độ phòng là bao nhiêu"}]},
        {"role": "model", "parts": [{"text": "Dùng /status để xem nhiệt độ, độ ẩm, khói và gas mới nhất."}]},
        {"role": "user",  "parts": [{"text": "kể chuyện cười"}]},
        {"role": "model", "parts": [{"text": "Mình là bot theo dõi ESP32, mình trả lời ngắn thôi. Bạn cần /menu không?"}]},
    ]


    payload = {
    "systemInstruction": {"role": "user", "parts": [{"text": system_prompt}]},
    "contents": FEW_SHOTS + [{
        "role": "user",
        "parts": [{"text": f"Ngữ cảnh: {ctx_text}\n\nNgười dùng: {user_text}"}]
    }],
    "generationConfig": {"temperature": 0.2, "maxOutputTokens": 120}
    }


    try:
        async with session.post(url, json=payload, timeout=AI_TIMEOUT) as r:
            data = await r.json()
            if 200 <= r.status < 300:
                # Lấy text theo cấu trúc chuẩn của Gemini
                text = ""
                try:
                    text = data["candidates"][0]["content"]["parts"][0]["text"]
                except Exception:
                    text = str(data)
                text = (text or "").strip()
                if not text:
                    text = "🤖 (AI không trả về nội dung.)"
                return True, text
            else:
                return False, f"AI HTTP {r.status}: {data}"
    except Exception as e:
        return False, f"AI error: {e}"

# Xử lý tin nhắn dạng text (từ khóa)
async def reply_text(update: Update, context: ContextTypes.DEFAULT_TYPE):
    txt = (update.message.text or "").lower().strip()

    if "bật" in txt or "mở" in txt or "on" in txt:
        async with aiohttp.ClientSession(timeout=HTTP_TIMEOUT) as s:
            ok, res = await aio_post(s, FEED_CMD, "on")
        await update.message.reply_text("✅ Đã gửi lệnh <b>ON</b>." if ok else f"❌ {res}", parse_mode="HTML")

    elif "tắt" in txt or "off" in txt:
        async with aiohttp.ClientSession(timeout=HTTP_TIMEOUT) as s:
            ok, res = await aio_post(s, FEED_CMD, "off")
        await update.message.reply_text("✅ Đã gửi lệnh <b>OFF</b>." if ok else f"❌ {res}", parse_mode="HTML")

    elif "test" in txt:
        async with aiohttp.ClientSession(timeout=HTTP_TIMEOUT) as s:
            ok, res = await aio_post(s, FEED_CMD, "test")
        await update.message.reply_text("✅ Đã gửi lệnh <b>Test</b>." if ok else f"❌ {res}", parse_mode="HTML")

    elif any(k in txt for k in ["trạng thái", "status", "tất cả", "tat ca", "all"]):
        async with aiohttp.ClientSession(timeout=HTTP_TIMEOUT) as s:
            okT, t = await aio_latest(s, FEED_TEMP)
            okH, h = await aio_latest(s, FEED_HUM)
            okG, g = await aio_latest(s, FEED_GAS)
            okD, d = await aio_latest(s, FEED_DUST)

        if not (okT and okH and okG and okD):
            msg = "❌ Lỗi đọc feed:\n"
            for ok, name, val in [(okT,FEED_TEMP,t),(okH,FEED_HUM,h),(okG,FEED_GAS,g),(okD,FEED_DUST,d)]:
                if not ok: msg += f"- {name}: {val}\n"
            await update.message.reply_text(msg)
            return

        def v(x): return x.get("value") if isinstance(x, dict) else "?"
        pretty = (
            "📊 <b>ESP32 Status</b>\n"
            f"🌡️ Temp: {v(t)} °C\n"
            f"💧 Hum : {v(h)} %\n"
            f"🧪 Gas : {v(g)}\n"
            f"🌫️ Dust: {v(d)}"
        )
        await update.message.reply_text(pretty, parse_mode="HTML")

    elif any(k in txt for k in ["nhiệt độ", "nhiet do", "nhiệt", "nhiet", "temp", "temperature"]):
        async with aiohttp.ClientSession(timeout=HTTP_TIMEOUT) as s:
            ok, d = await aio_latest(s, FEED_TEMP)
        if ok:
            val = d.get("value")
            await update.message.reply_text(f"🌡️ Nhiệt độ hiện tại: <b>{val} °C</b>", parse_mode="HTML")
        else:
            await update.message.reply_text(f"❌ Lỗi đọc nhiệt độ: {d}")

    elif any(k in txt for k in ["độ ẩm", "do am", "độÂm", "humidity", "hum", "ẩm", "am"]):
        async with aiohttp.ClientSession(timeout=HTTP_TIMEOUT) as s:
            ok, d = await aio_latest(s, FEED_HUM)
        if ok:
            val = d.get("value")
            await update.message.reply_text(f"💧 Độ ẩm hiện tại: <b>{val} %</b>", parse_mode="HTML")
        else:
            await update.message.reply_text(f"❌ Lỗi đọc độ ẩm: {d}")

    elif any(k in txt for k in ["gas", "khí gas", "mq2"]):
        async with aiohttp.ClientSession(timeout=HTTP_TIMEOUT) as s:
            ok, d = await aio_latest(s, FEED_GAS)
        if ok:
            val = d.get("value")
            await update.message.reply_text(f"🧪 Gas (MQ-2): <b>{val}</b>", parse_mode="HTML")
        else:
            await update.message.reply_text(f"❌ Lỗi đọc gas: {d}")

    elif any(k in txt for k in ["bụi", "bui", "dust", "pm"]):
        async with aiohttp.ClientSession(timeout=HTTP_TIMEOUT) as s:
            ok, d = await aio_latest(s, FEED_DUST)
        if ok:
            val = d.get("value")
            await update.message.reply_text(f"🌫️ Bụi: <b>{val}</b>", parse_mode="HTML")
        else:
            await update.message.reply_text(f"❌ Lỗi đọc bụi: {d}")
    else:
    # Lấy context nhanh từ các feed
        ctx_lines = []
        async with aiohttp.ClientSession(timeout=HTTP_TIMEOUT) as s:
            okT, t = await aio_latest(s, FEED_TEMP)
            okH, h = await aio_latest(s, FEED_HUM)
            okG, g = await aio_latest(s, FEED_GAS)
            okD, d = await aio_latest(s, FEED_DUST)
        def _v(x): return (x.get("value") if isinstance(x, dict) else "?")
        if okT: ctx_lines.append(f"Temp={_v(t)} °C")
        if okH: ctx_lines.append(f"Hum={_v(h)} %")
        if okG: ctx_lines.append(f"Gas={_v(g)}")
        if okD: ctx_lines.append(f"Dust={_v(d)}")
        ctx = " | ".join(ctx_lines) if ctx_lines else "no sensor context"

        # Gọi AI
        async with aiohttp.ClientSession(timeout=AI_TIMEOUT) as s:
            ok, ans = await ai_chat(s, txt, ctx)
        if ok:
            await update.message.reply_text(ans, parse_mode="HTML")
        else:
            await update.message.reply_text("🤖 Mình chưa hiểu, gõ /menu để xem lệnh.")




# ---------- main ----------
def main():
    app = ApplicationBuilder().token(BOT_TOKEN).build()
    app.add_handler(CommandHandler("start", start))
    app.add_handler(CommandHandler("help", help_cmd))
    app.add_handler(CommandHandler("on", cmd_on))
    app.add_handler(CommandHandler("off", cmd_off))
    app.add_handler(CommandHandler("test", cmd_test))
    app.add_handler(CommandHandler("status", cmd_status))
    app.add_handler(CallbackQueryHandler(on_button))
    app.add_handler(MessageHandler(filters.TEXT & ~filters.COMMAND, reply_text))
    app.add_handler(MessageHandler(filters.COMMAND, reply_text))  # xử lý lệnh từ bàn phím
    print("✅ Bot is running (Adafruit IO). Press Ctrl+C to stop.")
    app.run_polling()

if __name__ == "__main__":
    main()
