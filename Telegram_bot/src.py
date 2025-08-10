# pip install python-telegram-bot==20.7 aiohttp
from telegram import Update, InlineKeyboardMarkup, InlineKeyboardButton
from telegram.ext import ApplicationBuilder, CommandHandler, ContextTypes, MessageHandler, filters
import asyncio
import aiohttp
import json

BOT_TOKEN = "8105980539:AAE8fjSNKsqte2icOchMXt1u9RL0qWJb_QU"
ESP32_BASE = "http://192.168.1.29"
HTTP_TIMEOUT = aiohttp.ClientTimeout(total=5)

# Lưu tạm chat_id sau khi /start (in-memory)
LAST_CHAT_ID: int | None = None

ESP32_API = {
    "on":     f"{ESP32_BASE}/on",
    "off":    f"{ESP32_BASE}/off",
    "test":   f"{ESP32_BASE}/test",
    "status": f"{ESP32_BASE}/status",
}

def main_menu_markup() -> InlineKeyboardMarkup:
    kb = [
        [InlineKeyboardButton("🔊 ON", callback_data="on"),
         InlineKeyboardButton("🔇 OFF", callback_data="off")],
        [InlineKeyboardButton("🧪 Test buzzer", callback_data="test"),
         InlineKeyboardButton("📊 Status", callback_data="status")],
        [InlineKeyboardButton("❓ Help", callback_data="help")]
    ]
    return InlineKeyboardMarkup(kb)

async def http_get(session: aiohttp.ClientSession, url: str) -> tuple[bool, str]:
    try:
        async with session.get(url) as resp:
            text = await resp.text()
            if resp.status == 200:
                return True, text
            return False, f"HTTP {resp.status}: {text}"
    except Exception as e:
        return False, f"Lỗi kết nối: {e}"

# /start
async def start(update: Update, context: ContextTypes.DEFAULT_TYPE):
    global LAST_CHAT_ID
    LAST_CHAT_ID = update.effective_chat.id
    msg = (
        "👋 <b>Xin chào</b>! Đây là bot điều khiển <i>ESP32</i> của bạn.\n\n"
        "Dùng các lệnh:\n"
        "• /on – Bật thiết bị\n"
        "• /off – Tắt thiết bị\n"
        "• /test – Test buzzer\n"
        "• /status – Xem dữ liệu cảm biến\n"
        "• /help – Hướng dẫn\n\n"
        f"🖧 ESP32: <code>{ESP32_BASE}</code>"
    )
    await update.message.reply_text(msg, parse_mode="HTML", reply_markup=main_menu_markup())

# /help
async def help_cmd(update: Update, context: ContextTypes.DEFAULT_TYPE):
    await start(update, context)

# /on
async def cmd_on(update: Update, context: ContextTypes.DEFAULT_TYPE):
    async with aiohttp.ClientSession(timeout=HTTP_TIMEOUT) as s:
        ok, res = await http_get(s, ESP32_API["on"])
    await update.message.reply_text("✅ Đã gửi lệnh <b>ON</b>." if ok else f"❌ {res}", parse_mode="HTML")

# /off
async def cmd_off(update: Update, context: ContextTypes.DEFAULT_TYPE):
    async with aiohttp.ClientSession(timeout=HTTP_TIMEOUT) as s:
        ok, res = await http_get(s, ESP32_API["off"])
    await update.message.reply_text("✅ Đã gửi lệnh <b>OFF</b>." if ok else f"❌ {res}", parse_mode="HTML")

# /test
async def cmd_test(update: Update, context: ContextTypes.DEFAULT_TYPE):
    async with aiohttp.ClientSession(timeout=HTTP_TIMEOUT) as s:
        ok, res = await http_get(s, ESP32_API["test"])
    await update.message.reply_text("✅ Đã gửi lệnh <b>Test buzzer</b>." if ok else f"❌ {res}", parse_mode="HTML")

# /status
async def cmd_status(update: Update, context: ContextTypes.DEFAULT_TYPE):
    async with aiohttp.ClientSession(timeout=HTTP_TIMEOUT) as s:
        ok, res = await http_get(s, ESP32_API["status"])
    if not ok:
        await update.message.reply_text(f"❌ {res}")
        return
    # cố gắng parse JSON cho đẹp
    try:
        data = json.loads(res)
        pretty = (
            "📊 <b>ESP32 Status</b>\n"
            f"🕒 Time: {data.get('time','?')}\n"
            f"🌡️ Temp: {data.get('temp','?')} °C\n"
            f"💧 Hum: {data.get('hum','?')} %\n"
            f"🧪 Gas: {data.get('gas','?')}\n"
            f"🌫️ Dust: {data.get('dust','?')}"
        )
        await update.message.reply_text(pretty, parse_mode="HTML")
    except Exception:
        await update.message.reply_text(f"📄 {res}")

# Xử lý text thường (keyword)
async def reply_text(update: Update, context: ContextTypes.DEFAULT_TYPE):
    txt = (update.message.text or "").lower().strip()
    if "bật buzzer" in txt or "mở buzzer" in txt or txt == "on":
        await cmd_on(update, context)
    elif "tắt buzzer" in txt or "tắt thiết bị" in txt or txt == "off":
        await cmd_off(update, context)
    elif "test" in txt:
        await cmd_test(update, context)
    elif "status" in txt or "trạng thái" in txt:
        await cmd_status(update, context)
    elif "menu" in txt or "hướng dẫn" in txt or "help" in txt:
        await start(update, context)
    elif any(w in txt for w in ["xin chào","hello","hi","chào"]):
        await update.message.reply_text("👋 Chào bạn! Gõ /menu để xem nút bấm.", reply_markup=main_menu_markup())
    elif any(w in txt for w in ["tạm biệt","bye"]):
        await update.message.reply_text("👋 Tạm biệt! Hẹn gặp lại.")
    else:
        await update.message.reply_text("🤖 Chưa hiểu. Gõ 'menu' để xem lệnh.")

# Xử lý nút bấm (callback data)
from telegram.ext import CallbackQueryHandler
async def on_button(update: Update, context: ContextTypes.DEFAULT_TYPE):
    query = update.callback_query
    await query.answer()
    fake_update = Update(update.update_id, message=update.effective_message)
    if query.data == "on":
        await cmd_on(fake_update, context)
    elif query.data == "off":
        await cmd_off(fake_update, context)
    elif query.data == "test":
        await cmd_test(fake_update, context)
    elif query.data == "status":
        await cmd_status(fake_update, context)
    elif query.data == "help":
        await start(fake_update, context)

# Gửi thông báo chủ động tới chat cuối cùng (dùng trong nơi khác)
async def notify_last_chat(application, message: str):
    if LAST_CHAT_ID:
        await application.bot.send_message(chat_id=LAST_CHAT_ID, text=message, parse_mode="HTML")

# Main
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
    print("✅ Bot is running. Press Ctrl+C to stop.")
    app.run_polling()

if __name__ == "__main__":
    main()
