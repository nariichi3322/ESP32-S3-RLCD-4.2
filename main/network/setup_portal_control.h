// 声明配网热点启停请求与验证结果交接接口。
#pragma once

bool request_setup_portal_start();
bool setup_portal_start_requested();
bool request_setup_portal_stop();
bool setup_portal_stop_requested();
void complete_setup_portal_stop_request();
bool prepare_setup_portal_result_delivery();
