/*
######################################################################
##      Integração das tecnologias da REMOTE IO com Node IOT        ##
##                          Version 1.0                             ##
##   Código base para implementação de projetos de digitalização de ##
##   processos, automação, coleta de dados e envio de comandos com  ##
##   controle embarcado e na nuvem.                                 ##
##                                                                  ##
######################################################################
*/

#include "RemoteIO.h";
#include "index_html.h";

RemoteIO::RemoteIO()
{
  _appPort = 5000;

  server = new AsyncWebServer(80);
  deviceConfig = new Preferences();

  anchor_route = "http://anchor_IP/post-message";
  anchored_route = "http://anchored_IP/post-message";

  state = "";
  token = "";
    
  configurations = configurationDocument.to<JsonArray>();
  setIO = configurations.createNestedObject();

  Connected = false;
  Socketed = 0;
  messageTimestamp = 0;

  connection_state = INICIALIZATION;
  next_state = INICIALIZATION;

  start_debounce_time = 0;
  start_browsing_time = 0;
  start_reconnect_time = 0;
  start_config_time = 0;
  start_reset_time = 0;

  lastIP_index = -1;
  anchored = false;
  anchoring = false;
}

void RemoteIO::begin()
{
  Serial.begin(115200);

  // Abre a partição chamada "deviceConfig" com permissão de escrita e leitura
  deviceConfig->begin("deviceConfig", false);

  // Recupera configurações do dispositivo da NVS. Se não existir, retorna um valor padrão
  String NVS_SSID = deviceConfig->getString("ssid", "");
  String NVS_PASSWORD = deviceConfig->getString("password", "");
  String NVS_COMPANYNAME = deviceConfig->getString("companyName", "");
  String NVS_DEVICEID = deviceConfig->getString("deviceId", "");
  String NVS_IOSETTINGS = deviceConfig->getString("ioSettings", "");

  Serial.print("ioSettings: ");
  Serial.println(NVS_IOSETTINGS);

  // Nenhuma rede salva, serve a página html para configuração
  if (NVS_SSID.length() == 0 || NVS_PASSWORD.length() == 0 || NVS_COMPANYNAME.length() == 0 || NVS_DEVICEID.length() == 0)
  {
    startAccessPoint();
  }
  else
  {
    deviceConfig->end();

    _ssid = NVS_SSID;
    _password = NVS_PASSWORD;
    _companyName = NVS_COMPANYNAME;
    _deviceId = NVS_DEVICEID;

    //appBaseUrl = "https://dev.nodeiot.app.br/api";
    appBaseUrl = "https://api.nodeiot.app.br/api";
    appVerifyUrl = appBaseUrl + "/devices/verify";
    appPostData = appBaseUrl + "/broker/data/";
    appSideDoor = appBaseUrl + "/devices/devicedisconnected";
    appPostDataFromAnchored = appBaseUrl + "/broker/ahamdata";
    appLastDataUrl = appBaseUrl + "/devices/getdata/" + _companyName + "/" + _deviceId;

    // Conecta à plataforma
    nodeIotConnection();

    String LOCAL_DOMAIN = String("niot-") + String(_deviceId);

    // Configuração do mDNS (opcional)
    if (!MDNS.begin(LOCAL_DOMAIN)) 
    {
      Serial.println("Erro ao configurar o mDNS");
    }

    AsyncCallbackJsonWebHandler* handler = new AsyncCallbackJsonWebHandler("/post-message", [this](AsyncWebServerRequest *request, JsonVariant &json) {
      StaticJsonDocument<250> data;
      String response;
      if (json.is<JsonArray>())
      {
        data = json.as<JsonArray>();
      }
      else if (json.is<JsonObject>())
      {
        data = json.as<JsonObject>();
      }
      
      Serial.print("[AsyncCallback]: ");
      Serial.println(data.as<String>());

      // identifica dispositivo desconectado
      if (data.containsKey("status"))
      {
        if (connection_state == CONNECTED)
        {
          data.remove("status");
          data["ipAddress"] = request->client()->remoteIP().toString();

          serializeJson(data, send_to_niot_buffer);

          if (espPOST(appSideDoor, "", send_to_niot_buffer) == HTTP_CODE_OK)
          {
            send_to_niot_buffer.clear();
            data.clear();
            
            if (anchoring) 
            {
              data["msg"] = "ok";
              anchoring = false;
            }
            else data["msg"] = "received";

            serializeJson(data, response);
            request->send(200, "application/json", response);
          }
          else 
          {
            send_to_niot_buffer.clear();
            data.clear();
            data["msg"] = "post to niot failed";
            serializeJson(data, response);
            request->send(500, "application/json", response);
          }
        }
        else 
        {
          if (request->client()->remoteIP().toString() == anchor_IP) 
          {
            anchor_IP.clear();
            anchored = false;
          }

          data.clear();
          data["msg"] = "disconnected";
          serializeJson(data, response);
          request->send(500, "application/json", response);
        }
      }
      // ancorado recebe dataUpdate
      else if (data.containsKey("ref") && !data.containsKey("deviceId") && connection_state == DISCONNECTED)
      {
        anchor_IP.clear();
        anchor_IP = request->client()->remoteIP().toString();
        anchored = true;
        
        String ref = data["ref"].as<String>();
        setIO[ref]["value"] = data["value"];
        
        if (setIO[ref]["Mode"] == "OUTPUT")
        {
          updatePinOutput(ref);
        }

        data.clear();
        data["msg"] = "ok";
        serializeJson(data, response);
        request->send(200, "application/json", response);

        if (ref == "restart") ESP.restart();
      }
      // âncora recebe dados do ancorado
      else if (data.containsKey("deviceId"))
      {
        if (connection_state == CONNECTED)
        {
          serializeJson(data, send_to_niot_buffer);
          if (espPOST(appPostDataFromAnchored, "", send_to_niot_buffer) == HTTP_CODE_OK)
          {
            send_to_niot_buffer.clear();
            data.clear();
            data["msg"] = "ok";
            serializeJson(data, response);
            request->send(200, "application/json", response);
          }
          else 
          {
            send_to_niot_buffer.clear();
            data.clear();
            data["msg"] = "post to niot failed";
            serializeJson(data, response);
            request->send(500, "application/json", response);
          }
        }
        else 
        {
          data.clear();
          data["msg"] = "disconnected";
          serializeJson(data, response);
          request->send(500, "application/json", response);
        }
      }
      else 
      {
        data.clear();
        data["msg"] = "unhandled message";
        serializeJson(data, response);
        request->send(500, "application/json", response);
      }
    });
    server->addHandler(handler);
    server->onNotFound(std::bind(&RemoteIO::notFound, this, std::placeholders::_1));

    MDNS.addService("http", "tcp", 80);
    
    ArduinoOTA.begin();
  }
  server->begin();
}

void RemoteIO::checkResetting(long timeInterval)
{
  if (digitalRead(setIO["resetButton"]["pin"].as<int>()) == LOW)
  {
    if (start_reset_time == 0) start_reset_time = millis();
    else if (millis() - start_reset_time >= timeInterval)
    {
      deviceConfig->begin("deviceConfig", false);
      deviceConfig->clear();
      deviceConfig->end();
      delay(1000);
      ESP.restart();
    }
  }
  else start_reset_time = 0;
}

void RemoteIO::startAccessPoint()
{
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);

  // Inicia um Access Point no ESP32
  bool result = WiFi.softAP("ESP32-Config");
  if (!result) 
  {
    Serial.println("Erro ao configurar o ponto de acesso");
    ESP.restart();
  }

  Serial.println("Ponto de acesso iniciado");

  IPAddress IP = WiFi.softAPIP();
  Serial.print("IP do ponto de acesso: ");
  Serial.println(IP);

  String LOCAL_DOMAIN = String("niot-esp32");

  // Configuração do mDNS (opcional)
  if (!MDNS.begin(LOCAL_DOMAIN)) 
  {
    Serial.println("Erro ao configurar o mDNS");
  }

  start_config_time = millis();
  
  //
  // Servir a página HTML
  server->on("/", HTTP_GET, [this](AsyncWebServerRequest *request) {
      request->send_P(200, "text/html", page);
  });
  //

  // Captura e armazena as credenciais
  server->on("/get", HTTP_GET, [this](AsyncWebServerRequest *request) {
    //Serial.println("Requisição GET recebida"); // Adiciona uma mensagem de depuração

    if (request->hasParam("ssid") && request->hasParam("password") && request->hasParam("companyName") && request->hasParam("deviceId")) 
    {
      String new_ssid = request->getParam("ssid")->value();
      String new_password = request->getParam("password")->value();
      String new_companyName = request->getParam("companyName")->value();
      String new_deviceId = request->getParam("deviceId")->value();

      // Salva as credenciais
      deviceConfig->putString("ssid", new_ssid);
      deviceConfig->putString("password", new_password);
      deviceConfig->putString("companyName", new_companyName);
      deviceConfig->putString("deviceId", new_deviceId);
      deviceConfig->end();

      //Serial.println("Novas configurações de dispositivo salvas na memória não volátil.");
      request->send(200, "text/plain", "Credenciais recebidas. Reiniciando...");
      delay(3000);
      ESP.restart();
    } 
    else 
    {
      request->send(400, "text/plain", "Parâmetros ausentes");
    }
  });
}

void RemoteIO::loop()
{
  ArduinoOTA.handle();
  switchState();
  stateLogic();
  checkResetting(5000); // millisegundos
}

void RemoteIO::browseService(const char * service, const char * proto)
{
  Serial.printf("Browsing for service _%s._%s.local. ... ", service, proto);
  int n = MDNS.queryService(service, proto);
  if (n == 0) 
  {
    Serial.println("no services found");
    lastIP_index = -1;
  } 
  else 
  {
    Serial.print(n);
    Serial.println(" service(s) found");
    for (int i = 0; i < n; i++) 
    {
      // Print details for each service found
      Serial.print("  ");
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(MDNS.hostname(i));
      Serial.print(" (");
      Serial.print(MDNS.IP(i));
      Serial.print(":");
      Serial.print(MDNS.port(i));
      Serial.println(")");

      // pega o IP de um possível âncora
      if ((MDNS.hostname(i).indexOf("niot") != -1) || (MDNS.hostname(i).indexOf("esp32") != -1) || (MDNS.hostname(i).indexOf("esp8266") != -1))
      {
        if (i > lastIP_index)
        {
          lastIP_index = i;
          anchor_IP = MDNS.IP(i).toString();
          return;
        }
        else lastIP_index = -1;
      }
    }
  }
  Serial.println();
}

void RemoteIO::switchState()
{
  switch (connection_state)
  {
    case INICIALIZATION:
      if ((WiFi.status() == WL_CONNECTED) && (Connected == true))
      {
        Serial.println("[INICIALIZATION] vai pro CONNECTED");
        next_state = CONNECTED;
      }
      else
      {
        next_state = INICIALIZATION;
      }
      break;
      
    case CONNECTED:
      if (WiFi.status() != WL_CONNECTED)
      {
        Serial.println("[CONNECTED] vai pro NO_WIFI");
        next_state = NO_WIFI;
      }
      else if (!Connected)
      {
        Serial.println("[CONNECTED] vai pro DISCONNECTED");
        next_state = DISCONNECTED;
      }
      else 
      {
        next_state = CONNECTED;
      }
      break;
      
    case NO_WIFI:
      if (WiFi.status() == WL_CONNECTED)
      {
        Serial.println("[NO_WIFI] vai pro DISCONNECTED");
        start_debounce_time = 0;
        next_state = DISCONNECTED;
      }
      else 
      {
        next_state = NO_WIFI;
      }
      break;
      
    case DISCONNECTED:
      if (Connected)
      {
        Serial.println("[DISCONNECTED] vai pro CONNECTED");
        next_state = CONNECTED;
      }
      else if (WiFi.status() != WL_CONNECTED)
      {
        Serial.println("[DISCONNECTED] vai pro NO_WIFI");  
        next_state = NO_WIFI;
      }
      else 
      {
        next_state = DISCONNECTED;
      }
      break;
  }
  // updates current_state
  connection_state = next_state;
}

void RemoteIO::stateLogic()
{
  switch (connection_state)
  {
    case INICIALIZATION:
      
      socketIO.loop(); 
      if (Connected == false)
      {
        socketIOConnect();
      }
      break;
      
    case CONNECTED:
      
      socketIO.loop();
      if (setIO["disconnect"]["value"] == "1")
      {
        socketIO.sendEVENT("disconnect");
      }

      break;
      
    case NO_WIFI:

      if (millis() - start_reconnect_time >= 10000)
      {
        start_reconnect_time = millis();
        start_debounce_time = millis();
        nodeIotConnection(); 
      }
      break;

    case DISCONNECTED:
      
      if (setIO["disconnect"]["value"] == "0")
      {
        socketIO.loop();
        socketIOConnect();
      }
      
      // procura um âncora a cada 5 segundos
      if ((!anchored) && (millis() - start_browsing_time >= 5000))
      {
        browseService("http", "tcp");
        start_browsing_time = millis();

        // se encontrou possível âncora, tenta comunicação
        if (anchor_IP.length() > 0)
        {
          StaticJsonDocument<250> doc;
          doc["status"] = "disconnected";
          doc["mac"] = WiFi.macAddress();
          send_to_anchor_buffer.clear();
          serializeJson(doc, send_to_anchor_buffer);
          doc.clear();

          espPOST(anchor_route, "", send_to_anchor_buffer);          
        }
      }
      
      if (millis() - start_reconnect_time >= 60000)
      {
        start_reconnect_time = millis();
        start_debounce_time = millis();
        nodeIotConnection(); 
      }
      break;
  }
}

void RemoteIO::socketIOEvent(socketIOmessageType_t type, uint8_t *payload, size_t length)
{
  switch (type)
  {
  case sIOtype_DISCONNECT:
    Serial.printf("[IOc] Disconnected!\n");
    Connected = false;
    break;
  case sIOtype_CONNECT:
    Serial.printf("[IOc] Connected to url: %s\n", payload);
    socketIO.send(sIOtype_CONNECT, "/");
    break;
  case sIOtype_EVENT:
  {
    char *sptr = NULL;
    int id = strtol((char *)payload, &sptr, 10);

    //Serial.printf("[IOc] get event: %s id: %d\n", payload, id);
    
    if (id)
    {
      payload = (uint8_t *)sptr;
    }

    StaticJsonDocument<1024> doc;
    StaticJsonDocument<250> doc2;
    DeserializationError error = deserializeJson(doc, payload, length);

    if (error)
    {
      Serial.print(F("[IOc]: deserializeJson() failed: "));
      Serial.println(error.c_str());
      return;
    }

    String eventName = doc[0];

    //Serial.printf("[IOc] event name: %s\n", eventName.c_str());

    if (doc[1].containsKey("ipdest")) // modo âncora
    {
      doc2["ref"] = doc[1]["ref"];
      doc2["value"] = doc[1]["value"];
      
      anchored_IP = doc[1]["ipdest"].as<String>();
      serializeJson(doc2, send_to_anchored_buffer);
      
      doc2.clear();
      
      espPOST(anchored_route, "", send_to_anchored_buffer);
      send_to_anchored_buffer.clear();
    }
    else 
    {
      String ref = doc[1]["ref"];
      String value = doc[1]["value"];

      if (ref == "restart") ESP.restart();
      else if (ref == "resetButton")
      {
        deviceConfig->begin("deviceConfig", false);
        deviceConfig->clear();
        deviceConfig->end();
        delay(1000);
        ESP.restart();
      }

      setIO[ref]["value"] = value;

      if (setIO[ref]["Mode"] == "OUTPUT")
      {
        updatePinOutput(ref);
      }
    }
    doc.clear();
  }
  break;
  case sIOtype_ACK:
    Serial.printf("[IOc] get ack: %u\n", length);
    break;
  case sIOtype_ERROR:
    Serial.printf("[IOc] get error: %u\n", length);
    break;
  case sIOtype_BINARY_EVENT:
    Serial.printf("[IOc] get binary: %u\n", length);
    break;
  case sIOtype_BINARY_ACK:
    Serial.printf("[IOc] get binary ack: %u\n", length);
    break;
  }
}

void RemoteIO::nodeIotConnection()
{
  /*Conexão WiFi*/
  WiFi.disconnect(true);
  Connected = false;
  WiFi.mode(WIFI_STA);
  String hostname = String("niot-") + String(_deviceId);
  WiFi.setHostname(hostname.c_str());
  WiFi.begin(_ssid, _password);

  WiFi.waitForConnectResult();

  while (WiFi.status() != WL_CONNECTED)
  {
    if ((start_debounce_time != 0) && (millis() - start_debounce_time >= 2000))
    {
      WiFi.disconnect();
      Serial.println("[niotConnection] wifi.disconnect(), return");
      return;
    }
    delay(500);
    Serial.print(".");
  }
  
  Serial.printf("[nodeIotConnection] WiFi Connected %s\n", WiFi.localIP().toString().c_str());

  appVerifyUrl.replace(" ", "%20");
  appLastDataUrl.replace(" ", "%20");
  
  // Função de verificar permissão do dispositivo
  while (state != "accepted")
  {
    if ((start_debounce_time != 0) && (millis() - start_debounce_time >= 2000))
    {
      return;
    }
    tryAuthenticate();
  }
  
  String appSocketPath = "/socket.io/?token=" + token + "&EIO=4";

  // Depois de confirmado a permissão, chama função para carregar os ultimos valores salvos para/por este dispositivo
  fetchLatestData();

  // Conexão Websocket
  //<nome da companhia> e <nome do controlador> se tiver espaço subtituir por %20
  socketIO.begin(_appHost, _appPort, appSocketPath); // TODO: Não há necessidade do %22 (")
  socketIO.onEvent(std::bind(&RemoteIO::socketIOEvent, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
}

void RemoteIO::socketIOConnect()
{
  uint64_t now = millis();
  if (Socketed == 0)
  {
    StaticJsonDocument<JSON_DOCUMENT_CAPACITY> doc;
    JsonArray array = doc.to<JsonArray>();
    array.add("connection");
    JsonObject query = array.createNestedObject();
    query["Query"]["token"] = token;

    String output;
    serializeJson(doc, output);
    Socketed = socketIO.sendEVENT(output);
    Socketed = 1;
  }
  if ((Socketed == 1) && (now - messageTimestamp > 2000) && (Connected == 0))
  {
    StaticJsonDocument<JSON_DOCUMENT_CAPACITY> doc;
    JsonArray array = doc.to<JsonArray>();
    messageTimestamp = now;
    array.add("joinRoom");
    String output;
    serializeJson(doc, output);
    Connected = socketIO.sendEVENT(output);
    if (Connected) Serial.println("[socketIOConnect] Connected");
    else Serial.println("[socketIOConnect] Failed connecting");
  }
}

void RemoteIO::tryAuthenticate()
{
  WiFiClientSecure client;
  HTTPClient https;

  client.setInsecure();
  
  StaticJsonDocument<JSON_DOCUMENT_CAPACITY> document;
  String request;

  deviceConfig->begin("deviceConfig", false);
  String storedTimestamp = deviceConfig->getString("Timestamp", "");

  document["companyName"] = _companyName;
  document["deviceId"] = _deviceId;
  document["mac"] = WiFi.macAddress();
  document["ipAddress"] = WiFi.localIP().toString();
  document["settingsTimestamp"] = storedTimestamp;

  serializeJson(document, request);

  https.begin(client, appVerifyUrl);
  https.addHeader("Content-Type", "application/json");

  int statusCode = https.POST(request);
  
  String response = https.getString(); // obter resposta do servidor
  document.clear();
  deserializeJson(document, response);
  Serial.println(response);

  if (statusCode == HTTP_CODE_OK)
  {
    state = document["state"].as<String>();

    if (state != "accepted") 
    {
      document.clear();
      deviceConfig->end();
      https.end();
      return;
    }
    else if (document["settingsTimestamp"].as<String>() != storedTimestamp)
    {
      //Serial.println("[tryAuthenticate] timestamps diferentes");
      String ioSettings;
      serializeJson(document, ioSettings);
      deviceConfig->putString("ioSettings", ioSettings);
      document.clear();
      deserializeJson(document, ioSettings);
    }

    deviceConfig->end();
    token = document["token"].as<String>();
    extractIPAddress(document["serverAddr"].as<String>());
    Serial.println(document["serverAddr"].as<String>());

    for (size_t i = 0; i < document["gpio"].size(); i++)
    {
      String ref = document["gpio"][i]["ref"];

      int pin = document["gpio"][i]["pin"].as<int>();
      String type = document["gpio"][i]["type"];

      if (type == "INPUT" || type == "INPUT_ANALOG")
      {
        setIO[ref]["pin"] = pin;
        setIO[ref]["Mode"] = type;
        pinMode(pin, INPUT);
      }
      else if (type == "INPUT_PULLUP")
      {
        setIO[ref]["pin"] = pin;
        setIO[ref]["Mode"] = type;
        pinMode(pin, INPUT_PULLUP);
      }
      else if (type == "INPUT_PULLDOWN")
      {
        setIO[ref]["pin"] = pin;
        setIO[ref]["Mode"] = type;
        pinMode(pin, INPUT_PULLDOWN);
      }
      else if (type == "OUTPUT")
      {
        setIO[ref]["pin"] = pin;
        setIO[ref]["Mode"] = type;
        pinMode(pin, OUTPUT);
      } 
      else 
      {
        setIO[ref]["pin"] = pin;
        setIO[ref]["Mode"] = "N/L";
      }
    }
  }
  else
  {
    Serial.printf("[HTTP] POST... failed, code: %i,  error: %s\n", statusCode, https.errorToString(statusCode).c_str());
  }
  document.clear();
  https.end();
}

void RemoteIO::fetchLatestData()
{ 
  WiFiClient client;
  HTTPClient http;

  http.begin(client, appLastDataUrl);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + token);

  int statusCode = http.GET();

  if (statusCode == HTTP_CODE_OK)
  {
    StaticJsonDocument<JSON_DOCUMENT_CAPACITY> document;
    deserializeJson(document, http.getStream());

    for (size_t i = 0; i < document.size(); i++)
    {
      String auxRef = document[i]["ref"];
      String auxValue = document[i]["data"]["value"];

      if (auxValue == "null")
      {
        auxValue = "0";
      }
      
      setIO[auxRef]["value"] = auxValue;
      
      if (setIO[auxRef]["Mode"] == "OUTPUT")
      {
        updatePinOutput(auxRef);
      }
    }
    document.clear();
  }

  http.end();
}

void RemoteIO::extractIPAddress(String url)
{
  int startIndex = url.indexOf("//") + 2; // Encontra o início do endereço IP
  int endIndex = url.indexOf(":", startIndex); // Encontra o fim do endereço IP

  _appHost = url.substring(startIndex, endIndex); // Extrai o endereço IP
}

void RemoteIO::localHttpUpdateMsg (String ref, String value)
{
  StaticJsonDocument<250> doc;
  send_to_anchor_buffer.clear();
  setIO[ref]["value"] = value;
  doc["deviceId"] = _deviceId;
  doc["ref"] = ref;
  doc["value"] = value;
  serializeJson(doc, send_to_anchor_buffer);
  doc.clear();
  espPOST(anchor_route, "", send_to_anchor_buffer);
}

void RemoteIO::updatePinOutput(String ref)
{
  int PinRef = setIO[ref]["pin"].as<int>();
  int ValueRef = setIO[ref]["value"].as<int>();
  
  digitalWrite(PinRef, ValueRef);
}

void RemoteIO::updatePinInput(String ref)
{
  int pinRef = setIO[ref]["pin"].as<int>();
  String typeRef = setIO[ref]["Mode"].as<String>();
  int valueRef;

  if (typeRef == "INPUT" || typeRef == "INPUT_PULLDOWN" || typeRef == "INPUT_PULLUP")
  {
    valueRef = digitalRead(pinRef);
    if (connection_state == CONNECTED) espPOST(appPostData, ref, String(valueRef));
    else if (anchored) localHttpUpdateMsg(ref, String(valueRef));
  }
  else if (typeRef == "INPUT_ANALOG")
  {
    float value = analogRead(pinRef);
    if (connection_state == CONNECTED) espPOST(appPostData, ref, String(value));
    else if (anchored) localHttpUpdateMsg(ref, String(value));
  }
}

void RemoteIO::notFound(AsyncWebServerRequest *request)
{
  request->send(404, "application/json", "{\"message\":\"Not found\"}");
}

int RemoteIO::espPOST(String variable, String value)
{
    return espPOST(appPostData, variable, value);
}

int RemoteIO::espPOST(String Router, String variable, String value)
{
  if ((WiFi.status() == WL_CONNECTED))
  {
    String route;

    if (Router == anchor_route) route = "http://" + anchor_IP + "/post-message";
    else if (Router == anchored_route) route = "http://" + anchored_IP + "/post-message";
    else route = Router;


    WiFiClientSecure client;
    HTTPClient https;
    StaticJsonDocument<1024> document;
    String request;

    client.setInsecure();

    if (Router == appPostData)
    {
      document["deviceId"] = _deviceId;
      document["ref"] = variable;
      document["value"] = value;
      setIO[variable]["value"] = value;
      serializeJson(document, request);
    }
    else request = value; // mensagem serializada já está no parâmetro value
    
    https.begin(client, route); // HTTP
    https.addHeader("Content-Type", "application/json");
    https.addHeader("authorization", "Bearer " + token);

    //Serial.print("[espPOST] Request: ");
    //Serial.println(request);
    
    int httpCode = https.POST(request);

    String response = https.getString(); // obter resposta do servidor
    document.clear();
    deserializeJson(document, response);
    //Serial.println(response);

    if (httpCode == HTTP_CODE_OK)
    {
      if (Router == appSideDoor && document.containsKey("data"))
      {
        if (document["data"]["actived"].as<bool>() == true)
        {
          anchored_IP = document["data"]["ipdest"].as<String>();
          anchoring = true; 
        } 
      }
      else if (Router == anchor_route && document["msg"] == "ok")
      {
        anchored = true;
      }
    }
    else if (httpCode != HTTP_CODE_OK) 
    {
      Serial.printf("[HTTP] POST... failed, code: %i,  error: %s\n", httpCode, https.errorToString(httpCode).c_str());
      
      Serial.printf("msg: %s\n", document["msg"].as<String>());
      
      if (Router == anchored_route && variable != "restart")
      {
        Serial.println("avisa plataforma q n recebeu");
        // avisa plataforma que o ancorado não recebeu o dataUpdate
      }
      else if (Router == anchor_route)
      {
        Serial.println("[espPOST] perdi ancora");
        anchored = false;
      }
    }
    document.clear();
    https.end();
    return httpCode;
  }
  return 0;
}