# main.py
import asyncio
import json
from fastapi import FastAPI, WebSocket, WebSocketDisconnect, BackgroundTasks
from fastapi.responses import HTMLResponse
import os
import threading
import paho.mqtt.client as mqtt
from collections import defaultdict, deque
from dotenv import load_dotenv

load_dotenv()

MQTT_BROKER = os.getenv("MQTT_BROKER", "mqtt")
MQTT_PORT = int(os.getenv("MQTT_PORT", 1883))
MQTT_USER = os.getenv("MQTT_USER", "")
MQTT_PASS = os.getenv("MQTT_PASS", "")

app = FastAPI(title="HR Monitor Backend")

# in-memory store: device_id -> deque of readings
STORE = defaultdict(lambda: deque(maxlen=1000))

# connected websockets manager
class ConnectionManager:
    def __init__(self):
        self.active_connections: set[WebSocket] = set()

    async def connect(self, websocket: WebSocket):
        await websocket.accept()
        self.active_connections.add(websocket)

    def disconnect(self, websocket: WebSocket):
        self.active_connections.discard(websocket)

    async def broadcast(self, message: str):
        to_remove = []
        for conn in list(self.active_connections):
            try:
                await conn.send_text(message)
            except:
                to_remove.append(conn)
        for r in to_remove:
            self.disconnect(r)

mgr = ConnectionManager()

# MQTT client runs in background thread
def on_connect(client, userdata, flags, rc):
    print("MQTT connected with rc", rc)
    client.subscribe("hr/device/+/reading")

def on_message(client, userdata, msg):
    try:
        payload = msg.payload.decode()
        data = json.loads(payload)
        device_id = data.get("device_id", "unknown")
        STORE[device_id].append(data)
        # Broadcast to websockets (async) — use asyncio
        asyncio.run_coroutine_threadsafe(mgr.broadcast(json.dumps(data)), asyncio.get_event_loop())
    except Exception as e:
        print("Error processing message:", e)

def mqtt_thread():
    client = mqtt.Client()
    if MQTT_USER:
        client.username_pw_set(MQTT_USER, MQTT_PASS)
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(MQTT_BROKER, MQTT_PORT, 60)
    client.loop_forever()

@app.on_event("startup")
async def start_mqtt():
    # Start MQTT client thread
    thread = threading.Thread(target=mqtt_thread, daemon=True)
    thread.start()
    print("Started MQTT thread")

@app.get("/")
def index():
    return {"status": "OK"}

@app.get("/history/{device_id}")
def get_history(device_id: str, n: int = 100):
    items = list(STORE.get(device_id, []))[-n:]
    return {"device_id": device_id, "count": len(items), "data": items}

@app.websocket("/ws")
async def websocket_endpoint(ws: WebSocket):
    await mgr.connect(ws)
    try:
        while True:
            # keep connection alive
            data = await ws.receive_text()
            # echo or handle ping messages; clients may send JSON to request history
            await ws.send_text(json.dumps({"echo": data}))
    except WebSocketDisconnect:
        mgr.disconnect(ws)
