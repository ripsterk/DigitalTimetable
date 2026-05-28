# Smart Classroom Dashboard/API

Run on Ubuntu:

```bash
cd ~/Downloads/smart_classroom_project_v2/smart_classroom_dashboard
sudo apt update
sudo apt install python3-venv python3-pip libreoffice -y
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
python server.py
```

Open:

```text
http://localhost:5000
```

API examples:

```text
http://YOUR_IP:5000/api/current?sala=EC002
http://YOUR_IP:5000/api/current?sala=ED011&now=2026-05-27T16:04:00+03:00
http://YOUR_IP:5000/api/now
```

Optional OpenAI usage:

```bash
export OPENAI_API_KEY="your_new_key_here"
python server.py
```

The ESP32 should never contain the OpenAI key.
