#!/usr/bin/env python3
"""在电脑端模拟设备配网页及其保存反馈状态。"""

from __future__ import annotations

import argparse
import html
import json
import re
import time
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse


DEMO_DIR = Path(__file__).resolve().parent
REPO_ROOT = DEMO_DIR.parents[1]
ASSET_HEADER = REPO_ROOT / "main/network/wifi_portal_ui_assets.h"

SCENARIOS = {
    "form": ("", "", ""),
    "validating": (
        "pending",
        "PortalSaveValidatingTitle",
        "PortalSaveValidatingBody",
    ),
    "success": (
        "success",
        "PortalSaveConnectedTitle",
        "PortalSaveConnectedBody",
    ),
    "wifi-failed": (
        "",
        "PortalSaveWifiFailedTitle",
        "PortalSaveWifiFailedBody",
    ),
    "api-failed": (
        "",
        "PortalSaveWeatherApiFailedTitle",
        "PortalSaveWeatherApiFailedBody",
    ),
    "city-failed": (
        "",
        "PortalSaveWeatherCityInvalidTitle",
        "PortalSaveWeatherCityInvalidBody",
    ),
}

DEMO_CSS = """
body{padding-bottom:66px}
.demo-toolbar{
  position:fixed;z-index:20;left:50%;bottom:10px;transform:translateX(-50%);
  width:min(calc(100% - 20px),720px);padding:7px;
  display:flex;align-items:center;gap:6px;overflow-x:auto;
  border:1px solid #83909c;border-radius:8px;background:rgba(23,32,42,.96);
  box-shadow:0 8px 24px rgba(0,0,0,.2);color:#fff;
}
.demo-toolbar strong{flex:0 0 auto;padding:0 7px;font-size:12px}
.demo-toolbar a{
  flex:0 0 auto;padding:7px 9px;border:1px solid #66737e;border-radius:5px;
  color:#fff;text-decoration:none;font-size:12px;line-height:1;background:#25313b;
}
.demo-toolbar a.active{border-color:#fff;background:#fff;color:#17202a;font-weight:800}
"""

DEMO_WIFI_NETWORKS = (
    ("Redmi_8FA2", -31),
    ("家庭网络 5G", -45),
    ("WeatherClock-Lab-Long-SSID", -61),
    ("访客网络", -73),
)


def extract_raw_literal(name: str) -> str:
    source = ASSET_HEADER.read_text(encoding="utf-8")
    pattern = (
        rf'inline constexpr char {re.escape(name)}\[\] = '
        r'R"PORTAL\((.*?)\)PORTAL";'
    )
    match = re.search(pattern, source, flags=re.DOTALL)
    if not match:
        raise RuntimeError(f"无法从 {ASSET_HEADER} 读取 {name}")
    return match.group(1)


COMMON_CSS = extract_raw_literal("kCommonCss")
COMMON_SCRIPT_FORMAT = extract_raw_literal("kCommonScript")
FORM_HTML = extract_raw_literal("kFormHtml")

CATALOG = {
    entry["id"]: entry["zh-CN"]
    for entry in json.loads(
        (REPO_ROOT / "tools/ui_text_catalog.json").read_text(encoding="utf-8")
    )["entries"]
}


def catalog_text(identifier: str) -> str:
    return html.escape(CATALOG[identifier])


COMMON_SCRIPT = COMMON_SCRIPT_FORMAT % catalog_text("PortalSaving")


def toolbar(active: str) -> str:
    links = (
        ("form", "PortalClockSetupMode"),
        ("validating", "PortalValidating"),
        ("success", "PortalConnected"),
        ("wifi-failed", "PortalSaveWifiFailedTitle"),
        ("api-failed", "PortalSaveWeatherApiFailedTitle"),
        ("city-failed", "PortalSaveWeatherCityInvalidTitle"),
        ("offline", "PortalOfflineModeTitle"),
    )
    items = [
        "<nav class='demo-toolbar' aria-label='{}'><strong>{}</strong>".format(
            catalog_text("PortalClockSetupMode"),
            catalog_text("PortalClockSetupMode"),
        )
    ]
    for state, label_id in links:
        href = "/" if state == "form" else f"/?state={state}"
        active_class = " class='active'" if state == active else ""
        items.append(
            f"<a{active_class} href='{href}'>{catalog_text(label_id)}</a>"
        )
    items.append("</nav>")
    return "".join(items)


def feedback_html(state: str) -> str:
    style, title_id, body_id = SCENARIOS.get(state, SCENARIOS["form"])
    if not title_id:
        return ""
    role = "status" if state in {"validating", "success"} else "alert"
    class_name = f"feedback {style}".strip()
    return (
        f"<div class='{class_name}' role='{role}'>"
        f"<strong>{catalog_text(title_id)}</strong>{catalog_text(body_id)}</div>"
    )


def wifi_list_html() -> str:
    buttons = []
    for ssid, rssi in DEMO_WIFI_NETWORKS:
        safe_ssid = html.escape(ssid, quote=True)
        buttons.append(
            "<button type='button' class='wifi' "
            f"data-ssid='{safe_ssid}' onclick='pick(this.dataset.ssid)'>"
            f"<span>{safe_ssid}</span><b>{rssi} dBm</b></button>"
        )
    return (
        "<section class='wifi-section portal-panel'><div class='portal-panel-body'>"
        f"<div class='section-title'><span>{catalog_text('PortalNearbyWifi')}</span>"
        f"<a href='/'>{catalog_text('PortalScanAgain')}</a></div><div class='wifi-list'>"
        + "".join(buttons)
        + "</div></div></section>"
    )


def render_form(state: str = "form") -> str:
    if state == "offline":
        return render_offline_result(True)
    if state not in SCENARIOS:
        state = "form"
    form = FORM_HTML % (
        catalog_text("PortalWifiSectionTitle"),
        catalog_text("PortalWifiSectionDescription"),
        catalog_text("PortalPrimaryWifiSsidLabel"),
        catalog_text("PortalPrimaryWifiSsidPlaceholder"),
        html.escape("Redmi_8FA2", quote=True),
        catalog_text("PortalPrimaryWifiPasswordLabel"),
        catalog_text("PortalPrimaryWifiPasswordPlaceholder"),
        catalog_text("PortalPrimaryWifiPasswordHint"),
        catalog_text("PortalBackupWifiSsidLabel"),
        catalog_text("PortalOptional"),
        catalog_text("PortalBackupWifiSsidPlaceholder"),
        html.escape("备用热点", quote=True),
        catalog_text("PortalBackupWifiPasswordLabel"),
        catalog_text("PortalOptional"),
        catalog_text("PortalBackupWifiPasswordPlaceholder"),
        catalog_text("PortalBackupWifiPasswordHint"),
        catalog_text("PortalTimeSyncTitle"),
        catalog_text("PortalTimeSyncDescription"),
        catalog_text("PortalNtpServer"),
        catalog_text("PortalNtpServerPlaceholder"),
        html.escape("pool.ntp.org", quote=True),
        catalog_text("PortalNtpServerHint"),
        catalog_text("PortalWeatherCityLabel"),
        catalog_text("PortalOptional"),
        catalog_text("PortalWeatherCityPlaceholder"),
        html.escape("杭州", quote=True),
        catalog_text("PortalWeatherCityHint"),
        catalog_text("PortalSaveAndConnect"),
        catalog_text("PortalSaveStatus"),
        catalog_text("PortalOfflineModeTitle"),
        catalog_text("PortalOfflineModeDescription"),
        catalog_text("PortalLocalDateTime"),
        catalog_text("PortalStartOfflineMode"),
    )
    return (
        "<!doctype html><html lang='zh-CN'><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        f"<title>{catalog_text('PortalClockSetupMode')}</title>"
        f"<style>{COMMON_CSS}{DEMO_CSS}</style>"
        f"<script>{COMMON_SCRIPT}</script></head><body>"
        f"{toolbar(state)}"
        "<main class='portal-shell'><header class='portal-header'>"
        "<div class='brand-lockup'><div class='brand-mark'>42</div>"
        f"<div class='brand-copy'><h1>{catalog_text('PortalClockSetupMode')}</h1><p>{catalog_text('PortalWifiNtpSetup')}</p></div></div>"
        f"<div class='ap-meta'><span>{catalog_text('PortalDeviceHotspot')}</span><strong>WeatherClock-Demo</strong></div>"
        "</header><section class='portal-form-shell'>"
        f"{feedback_html(state)}{form}</section>{wifi_list_html()}</main>"
        "</body></html>"
    )


def render_result(state: str) -> str:
    states = {
        "validating": (
            "PortalValidating",
            "PortalSaveValidatingTitle",
            "PortalSaveValidatingBody",
        ),
        "success": (
            "PortalConnected",
            "PortalSaveConnectedTitle",
            "PortalSaveConnectedBody",
        ),
        "wifi-failed": (
            "PortalFailed",
            "PortalSaveWifiFailedTitle",
            "PortalSaveWifiFailedBody",
        ),
        "api-failed": (
            "PortalFailed",
            "PortalSaveWeatherApiFailedTitle",
            "PortalSaveWeatherApiFailedBody",
        ),
    }
    badge_id, title_id, body_id = states.get(state, states["validating"])
    poll_script = ""
    if state == "validating":
        poll_script = """
<script>
function poll(){
  fetch("/status",{cache:"no-store"}).then(function(response){
    if(response.status===200){location.replace("/result?state=success");return;}
    if(response.status===409){location.replace("/?state=api-failed");return;}
    setTimeout(poll,700);
  }).catch(function(){setTimeout(poll,900);});
}
setTimeout(poll,600);
</script>
"""
    return (
        "<!doctype html><html lang='zh-CN'><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        f"<title>{catalog_text('PortalClockSetupResult')}</title>"
        f"<style>{COMMON_CSS}{DEMO_CSS}</style>{poll_script}</head><body>"
        f"{toolbar(state)}"
        "<main class='result-shell'><section class='portal-panel result-panel'>"
        f"<div class='result-state'>{catalog_text(badge_id)}</div>"
        f"<h1>{catalog_text(title_id)}</h1><p>{catalog_text(body_id)}</p>"
        f"<div class='meta'>{catalog_text('PortalPrimaryWifi')}{catalog_text('PortalSeparator')}Redmi_8FA2<br>"
        f"{catalog_text('PortalBackupWifi')}{catalog_text('PortalSeparator')}{html.escape('备用热点')}<br>"
        f"{catalog_text('PortalWeatherCityLabel')}{catalog_text('PortalSeparator')}{html.escape('杭州')}<br>"
        f"{catalog_text('PortalLastWifiDisconnect')}{catalog_text('PortalSeparator')}0</div>"
        f"<a class='primary-link' href='/'>{catalog_text('PortalBackToSetup')}</a>"
        "</section></main></body></html>"
    )


def render_offline_result(saved: bool) -> str:
    badge_id = "PortalOfflineModeEnabled" if saved else "PortalFailed"
    title_id = "PortalOfflineModeTitle" if saved else "PortalInvalidDateTime"
    body_id = "PortalOfflineModeDescription" if saved else "PortalInvalidDateTime"
    return (
        "<!doctype html><html lang='zh-CN'><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        f"<title>{catalog_text('PortalOfflineModeTitle')}</title>"
        f"<style>{COMMON_CSS}{DEMO_CSS}</style></head><body>{toolbar('offline')}"
        "<main class='result-shell'><section class='portal-panel result-panel'>"
        f"<div class='result-state'>{catalog_text(badge_id)}</div><h1>{catalog_text(title_id)}</h1><p>{catalog_text(body_id)}</p>"
        f"<a class='primary-link' href='/'>{catalog_text('PortalBackToSetup')}</a>"
        "</section></main></body></html>"
    )


class PortalDemoHandler(BaseHTTPRequestHandler):
    validation_started_at = 0.0

    def log_message(self, fmt: str, *args: object) -> None:
        print(f"[portal-demo] {self.address_string()} {fmt % args}")

    def send_html(self, content: str, status: HTTPStatus = HTTPStatus.OK) -> None:
        body = content.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:
        request = urlparse(self.path)
        query = parse_qs(request.query)
        if request.path == "/":
            self.send_html(render_form(query.get("state", ["form"])[0]))
            return
        if request.path == "/result":
            self.send_html(render_result(query.get("state", ["validating"])[0]))
            return
        if request.path == "/status":
            ready = time.monotonic() - self.validation_started_at >= 2.4
            self.send_response(HTTPStatus.OK if ready else HTTPStatus.NO_CONTENT)
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        self.send_response(HTTPStatus.FOUND)
        self.send_header("Location", "/")
        self.end_headers()

    def do_POST(self) -> None:
        if urlparse(self.path).path != "/save":
            self.send_error(HTTPStatus.NOT_FOUND)
            return
        content_length = int(self.headers.get("Content-Length", "0"))
        form = parse_qs(self.rfile.read(content_length).decode("utf-8"))
        ssid = form.get("ssid", [""])[0].strip()
        manual_time = form.get("manual_time", [""])[0].strip()
        if not ssid:
            self.send_html(render_offline_result(bool(manual_time)))
            return
        type(self).validation_started_at = time.monotonic()
        self.send_html(render_result("validating"))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="天气时钟配网页电脑调试 Demo")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8096)
    parser.add_argument(
        "--render",
        choices=tuple(SCENARIOS) + ("offline",),
        help="输出指定状态 HTML 后退出，供自动化测试使用",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.render:
        print(render_form(args.render))
        return 0
    server = ThreadingHTTPServer((args.host, args.port), PortalDemoHandler)
    print(f"配网页 Demo 已启动：http://{args.host}:{args.port}/")
    print("按 Ctrl+C 停止。")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
