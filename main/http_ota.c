#include "http_ota.h"

static const char *TAG = "OTA_WEB_SERVER";
static esp_ota_handle_t ota_handle = 0;
httpd_handle_t server = NULL; // Webserver-Handle

#define APP_PATH "/spiffs/app.elf"  // 🔥 App wird hier gespeichert

esp_err_t app_upload_handler(httpd_req_t *req) {
	FILE *file = fopen(APP_PATH, "wb");
	if (!file) {
		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "⚠️ Konnte Datei nicht öffnen");
		return ESP_FAIL;
	}

	char buf[512];
	int received, total_bytes = 0;

	while ((received = httpd_req_recv(req, buf, sizeof(buf))) > 0) {
		fwrite(buf, 1, received, file);
		total_bytes += received;
	}

	fclose(file);
	ESP_LOGI("APP Download","App gespeichert: %d Bytes\n", total_bytes);
	
	httpd_resp_sendstr(req, "Upload erfolgreich!");
	return ESP_OK;
}

esp_err_t ota_update_handler(httpd_req_t *req) {
	esp_err_t err;
	char buf[1024];
	int received;
	const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);

	ESP_LOGI(TAG, "Start OTA update");
	err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_ota_begin failed");
		return ESP_FAIL;
	}

	while ((received = httpd_req_recv(req, buf, sizeof(buf))) > 0) {
		err = esp_ota_write(ota_handle, buf, received);
		if (err != ESP_OK) {
			ESP_LOGE(TAG, "Error during OTA write");
			return ESP_FAIL;
		}
	}

	if (received < 0) {
		ESP_LOGE(TAG, "Error receiving OTA data");
		return ESP_FAIL;
	}

	ESP_LOGI(TAG, "OTA update complete");
	err = esp_ota_end(ota_handle);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "OTA end failed");
		return ESP_FAIL;
	}

	esp_err_t set_boot = esp_ota_set_boot_partition(update_partition);
	if (set_boot != ESP_OK) {
		ESP_LOGE(TAG, "esp_ota_set_boot_partition failed");
		return ESP_FAIL;
	}

	httpd_resp_set_hdr(req, "Set-Cookie", "session=invalid; Path=/");
	httpd_resp_send(req, "Update Complete, Restarting...", HTTPD_RESP_USE_STRLEN);
	vTaskDelay(1000 / portTICK_PERIOD_MS);
	esp_restart();
	return ESP_OK;
}

esp_err_t app_page_handler(httpd_req_t *req) {
	char cookie[64];
	//if (httpd_req_get_hdr_value_str(req, "Cookie", cookie, sizeof(cookie)) == ESP_OK) {
	//	if (strstr(cookie, "session=valid")) {
			const char *html_page =
				"<!DOCTYPE html><html lang='de'><head>"
				"<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1.0'>"
				"<title>ESP32 APP UPLOADER</title>"
				"<style>body { font-family: Arial, sans-serif; text-align: center; padding: 20px; }"
				"h2 { color: #333; } #fileInput { margin: 20px; }"
				"#uploadBtn { padding: 10px 20px; background: #28a745; color: white; border: none; cursor: pointer; }"
				"#uploadBtn:disabled { background: #ccc; cursor: not-allowed; } #status { margin-top: 20px; font-weight: bold; }"
				"</style></head><body><h2>ESP32 App Upload</h2>"
				"<input type='file' id='fileInput'><button id='uploadBtn' disabled>Upload starten</button>"
				"<p id='status'></p><script>const fileInput = document.getElementById('fileInput');"
				"const uploadBtn = document.getElementById('uploadBtn'); const status = document.getElementById('status');"
				"fileInput.addEventListener('change',()=>{ uploadBtn.disabled=!fileInput.files.length; });"
				"uploadBtn.addEventListener('click',()=>{ if (!fileInput.files.length) return;"
				"const file = fileInput.files[0]; uploadBtn.disabled=true; status.innerText='Uploading...';"
				"fetch('/upload_app',{ method:'POST', body:file }).then(response=>response.text()).then(result=>{"
				"status.innerText=result; uploadBtn.disabled=false; }).catch(error=>{ status.innerText='Fehler beim Upload!';"
				"uploadBtn.disabled=false; }); });</script></body></html>";

			httpd_resp_set_type(req, "text/html");
			httpd_resp_send(req, html_page, HTTPD_RESP_USE_STRLEN);
			return ESP_OK;
		//}
	//}
	// Falls nicht eingeloggt: Weiterleitung zur Login-Seite
	// httpd_resp_set_status(req, "302 Found");
	// httpd_resp_set_hdr(req, "Location", "/login");
	// httpd_resp_send(req, NULL, 0);
	return ESP_OK;
}

esp_err_t index_page_handler(httpd_req_t *req) {
	char cookie[64];
	//if (httpd_req_get_hdr_value_str(req, "Cookie", cookie, sizeof(cookie)) == ESP_OK) {
	//	if (strstr(cookie, "session=valid")) {
			const char *html_page =
				"<!DOCTYPE html><html lang='de'><head>"
				"<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1.0'>"
				"<title>ESP32 OTA Update</title>"
				"<style>body { font-family: Arial, sans-serif; text-align: center; padding: 20px; }"
				"h2 { color: #333; } #fileInput { margin: 20px; }"
				"#uploadBtn { padding: 10px 20px; background: #28a745; color: white; border: none; cursor: pointer; }"
				"#uploadBtn:disabled { background: #ccc; cursor: not-allowed; } #status { margin-top: 20px; font-weight: bold; }"
				"</style></head><body><h2>ESP32 OTA Firmware Update</h2>"
				"<input type='file' id='fileInput'><button id='uploadBtn' disabled>Update starten</button>"
				"<p id='status'></p><script>const fileInput = document.getElementById('fileInput');"
				"const uploadBtn = document.getElementById('uploadBtn'); const status = document.getElementById('status');"
				"fileInput.addEventListener('change',()=>{ uploadBtn.disabled=!fileInput.files.length; });"
				"uploadBtn.addEventListener('click',()=>{ if (!fileInput.files.length) return;"
				"const file = fileInput.files[0]; uploadBtn.disabled=true; status.innerText='Uploading...';"
				"fetch('/update',{ method:'POST', body:file }).then(response=>response.text()).then(result=>{"
				"status.innerText=result; uploadBtn.disabled=false; }).catch(error=>{ status.innerText='Fehler beim Upload!';"
				"uploadBtn.disabled=false; }); });</script></body></html>";

			httpd_resp_set_type(req, "text/html");
			httpd_resp_send(req, html_page, HTTPD_RESP_USE_STRLEN);
			return ESP_OK;
		//}
	//}
	// Falls nicht eingeloggt: Weiterleitung zur Login-Seite
	// httpd_resp_set_status(req, "302 Found");
	// httpd_resp_set_hdr(req, "Location", "/login");
	// httpd_resp_send(req, NULL, 0);
	return ESP_OK;
}

esp_err_t auth_handler(httpd_req_t *req) {
	char content[100];
	int ret = httpd_req_recv(req, content, sizeof(content) - 1);
	if (ret <= 0) return ESP_FAIL;

	content[ret] = '\0';  // Null-terminieren
	if (strstr(content, "\"user\":\"admin\"") && strstr(content, "\"pass\":\"esp32\"")) {
		httpd_resp_set_hdr(req, "Set-Cookie", "session=valid; Path=/");
		httpd_resp_set_hdr(req, "Location", "/");
		httpd_resp_set_status(req, "302 Found");
		httpd_resp_send(req, NULL, 0);  // Keine Nachricht nötig, nur Redirect
	} else {
		httpd_resp_set_hdr(req, "Set-Cookie", "session=invalid; Path=/");
		httpd_resp_send(req, "Falsche Anmeldedaten!", HTTPD_RESP_USE_STRLEN);
	}
	return ESP_OK;
}

esp_err_t secure_handler(httpd_req_t *req) {
	char cookie[64];
	if (httpd_req_get_hdr_value_str(req, "Cookie", cookie, sizeof(cookie)) == ESP_OK) {
		if (strstr(cookie, "session=valid")) {
			httpd_resp_send(req, "<h2>Willkommen, Admin!</h2>", HTTPD_RESP_USE_STRLEN);
			httpd_resp_set_hdr(req, "Location", "/");
			httpd_resp_send(req, NULL, 0);  // Keine Nachricht nötig, nur Redirect
			return ESP_OK;
		}
	}

	httpd_resp_set_status(req, "403 Forbidden");
	httpd_resp_send(req, "Nicht autorisiert!", HTTPD_RESP_USE_STRLEN);
	return ESP_FAIL;
}

esp_err_t login_page_handler(httpd_req_t *req) {
	const char *html_page =
		"<!DOCTYPE html><html lang='de'><head>"
		"<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1.0'>"
		"<title>ESP32 OTA Update Login</title>"
		"</head><body>"
		"<h2>ESP32 Login</h2>"
		"<form onsubmit='login(event)'>"
		"Benutzername: <input type='text' id='user'><br>"
		"Passwort: <input type='password' id='pass'><br>"
		"<button type='submit'>Login</button></form>"
		"<script>"
		"function login(event) {"
		" event.preventDefault();"
		" fetch('/auth', { method: 'POST', headers: { 'Content-Type': 'application/json' },"
		" body: JSON.stringify({ user: document.getElementById('user').value, pass: document.getElementById('pass').value })"
		"}).then(res => res.text());.then(msg => alert(msg));"

		"}"
		"</script></body></html>";

	httpd_resp_set_type(req, "text/html");
	httpd_resp_send(req, html_page, HTTPD_RESP_USE_STRLEN);
	return ESP_OK;
}

// HTTP-Endpunkt registrieren
httpd_uri_t app_upload = {
	.uri      = "/upload_app",
	.method   = HTTP_POST,
	.handler  = app_upload_handler,
	.user_ctx = NULL
};

httpd_uri_t app_control = {
	.uri      = "/app",
	.method   = HTTP_GET,
	.handler  = app_page_handler,
	.user_ctx = NULL
};

httpd_uri_t uri_index = {
	.uri = "/",
	.method = HTTP_GET,
	.handler = index_page_handler,
	.user_ctx = NULL
};

httpd_uri_t uri_update = {
	.uri = "/update",
	.method = HTTP_POST,
	.handler = ota_update_handler,
	.user_ctx = NULL
};

httpd_uri_t login_page = {
	.uri = "/login",
	.method = HTTP_GET,
	.handler = login_page_handler
};

httpd_uri_t auth = {
	.uri = "/auth",
	.method = HTTP_POST,
	.handler = auth_handler
};

httpd_uri_t secure = {
	.uri = "/secure",
	.method = HTTP_GET,
	.handler = secure_handler
};

// Webserver starten
void start_webserver(void) {
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	if (httpd_start(&server, &config) == ESP_OK) {
		httpd_register_uri_handler(server, &login_page);
		httpd_register_uri_handler(server, &auth);
		httpd_register_uri_handler(server, &secure);
		httpd_register_uri_handler(server, &uri_update);
		httpd_register_uri_handler(server, &uri_index);
		httpd_register_uri_handler(server, &app_upload);
		httpd_register_uri_handler(server, &app_control);
		ESP_LOGI(TAG, "Webserver gestarted.");
	}
}

// Webserver stoppen
void stop_webserver(void) {
	if (server) {
		httpd_stop(server);
		server = NULL;
		ESP_LOGI(TAG, "Webserver gestoppt.");
	}
}