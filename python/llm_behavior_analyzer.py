#!/usr/bin/env python3
"""Optional behavior-analysis enrichment service.

The C++ pipeline can run without this file. This script documents the Python
side of the project and provides a tiny stdin/stdout compatible analyzer that
can be replaced by a real LLM HTTP service.
"""

from __future__ import annotations

import json
import os
import sys
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any

ARK_API_BASE_URL = os.environ.get("ARK_API_BASE_URL", "https://ark.cn-beijing.volces.com/api/v3/responses")
ARK_API_KEY = os.environ.get("ARK_API_KEY", "").strip()
ARK_MODEL = os.environ.get("ARK_MODEL", "doubao-seed-2-0-mini-260428")
ARK_TIMEOUT_SECONDS = float(os.environ.get("ARK_TIMEOUT_SECONDS", "60"))


def enrich(payload: dict[str, Any]) -> dict[str, Any]:
    events = payload.get("events", [])
    summary = {
        "event_count": len(events),
        "highest_risk": "low",
        "recommendation": "keep monitoring",
    }

    ranks = {"low": 0, "medium": 1, "high": 2, "critical": 3}
    highest = "low"
    for event in events:
        risk = str(event.get("risk", "low"))
        if ranks.get(risk, 0) > ranks[highest]:
            highest = risk

    summary["highest_risk"] = highest
    if highest in {"high", "critical"}:
        summary["recommendation"] = "alert driver and persist clip"

    model_result = call_model(payload, summary)
    if model_result is not None:
        return model_result

    return {"accepted": True, "summary": summary, "events": events}


def call_model(payload: dict[str, Any], summary: dict[str, Any]) -> dict[str, Any] | None:
    if not ARK_API_KEY:
        return None

    frame = payload.get("frame", {})
    image = frame.get("image", {}) if isinstance(frame, dict) else {}
    image_data = image.get("data", "") if isinstance(image, dict) else ""
    image_mime = image.get("mime", "image/jpeg") if isinstance(image, dict) else "image/jpeg"
    user_content: list[dict[str, Any]] = []
    if image_data:
        user_content.append(
            {
                "type": "input_image",
                "image_url": f"data:{image_mime};base64,{image_data}",
            }
        )
    user_content.append(
        {
            "type": "input_text",
            "text": json.dumps(
                {
                    "frame": {
                        "index": frame.get("index") if isinstance(frame, dict) else None,
                        "width": frame.get("width") if isinstance(frame, dict) else None,
                        "height": frame.get("height") if isinstance(frame, dict) else None,
                        "has_image": bool(image_data),
                    },
                    "events": payload.get("events", []),
                    "summary": summary,
                },
                ensure_ascii=False,
            ),
        }
    )

    request_body = {
        "model": ARK_MODEL,
        "input": [
            {
                "role": "system",
                "content": [
                    {
                        "type": "input_text",
                        "text": (
                            "You are a driving behavior analysis assistant. "
                            "Review each event using the camera frame when an image is provided, plus the "
                            "event bbox, objectClass, behavior, risk, and evidence. "
                            "Adjust risk only when the visual scene supports it. "
                            "If the bbox/object appears to be a false positive, set accepted to false for that event "
                            "and set risk to low. "
                            "Return only valid JSON with keys accepted, summary, and events. "
                            "accepted must be true. "
                            "Each returned event must keep the original trackId and behavior, may include accepted, "
                            "and each risk must be one of low, medium, high, or critical. "
                            "Add a short evidence or reason field explaining the review."
                        ),
                    }
                ],
            },
            {
                "role": "user",
                "content": user_content,
            },
        ],
    }

    request = urllib.request.Request(
        ARK_API_BASE_URL,
        data=json.dumps(request_body, ensure_ascii=False).encode("utf-8"),
        headers={
            "Content-Type": "application/json",
            "Authorization": f"Bearer {ARK_API_KEY}",
        },
        method="POST",
    )

    try:
        with urllib.request.urlopen(request, timeout=ARK_TIMEOUT_SECONDS) as response:
            raw = response.read().decode("utf-8")
        parsed = json.loads(raw)
    except (urllib.error.URLError, urllib.error.HTTPError, TimeoutError, OSError, json.JSONDecodeError):
        return None

    model_text = extract_text(parsed)
    if model_text:
        try:
            model_json = json.loads(normalize_json_text(model_text))
            if isinstance(model_json, dict):
                accepted = bool(model_json.get("accepted", True))
                model_json.setdefault("accepted", accepted)
                model_json.setdefault("summary", summary)
                model_json.setdefault("events", payload.get("events", []))
                return model_json
        except json.JSONDecodeError:
            pass

    return {
        "accepted": True,
        "summary": summary,
        "events": payload.get("events", []),
        "model_response": parsed,
    }


def extract_text(value: Any) -> str | None:
    if isinstance(value, str):
        return value
    if isinstance(value, list):
        for item in value:
            found = extract_text(item)
            if found:
                return found
        return None
    if isinstance(value, dict):
        for key in ("output_text", "text", "content", "message"):
            if key in value:
                found = extract_text(value[key])
                if found:
                    return found
        for item in value.values():
            found = extract_text(item)
            if found:
                return found
    return None


def normalize_json_text(text: str) -> str:
    stripped = text.strip()
    if stripped.startswith("```"):
        lines = stripped.splitlines()
        if lines and lines[0].startswith("```"):
            lines = lines[1:]
        if lines and lines[-1].strip() == "```":
            lines = lines[:-1]
        stripped = "\n".join(lines).strip()

    first_object = stripped.find("{")
    last_object = stripped.rfind("}")
    if first_object != -1 and last_object > first_object:
        return stripped[first_object : last_object + 1]
    return stripped


def main() -> int:
    if len(sys.argv) >= 2 and sys.argv[1] == "serve":
        port = int(sys.argv[2]) if len(sys.argv) >= 3 else 8000
        server = ThreadingHTTPServer(("0.0.0.0", port), AnalyzeHandler)
        print(f"llm_behavior_analyzer listening on 0.0.0.0:{port}", flush=True)
        server.serve_forever()
        return 0

    raw = sys.stdin.read() or "{}"
    payload = json.loads(raw)
    print(json.dumps(enrich(payload), ensure_ascii=False, indent=2))
    return 0


class AnalyzeHandler(BaseHTTPRequestHandler):
    def do_POST(self) -> None:
        if self.path != "/analyze":
            self.send_error(404)
            return

        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(length).decode("utf-8") if length > 0 else "{}"
        try:
            response = enrich(json.loads(raw))
            body = json.dumps(response, ensure_ascii=False).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            try:
                self.wfile.write(body)
            except BrokenPipeError:
                return
        except json.JSONDecodeError:
            self.send_error(400, "invalid json")

    def log_message(self, format: str, *args: Any) -> None:
        return


if __name__ == "__main__":
    raise SystemExit(main())
