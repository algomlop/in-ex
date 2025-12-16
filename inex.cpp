

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include "AudioFileSourceSPIFFS.h"
#include "AudioFileSourceID3.h"
#include "AudioGeneratorWAV.h"
#include "AudioGeneratorMP3.h" 
#include "AudioOutputI2S.h"
#include "AudioFileSourceBuffer.h" // <--- NUEVO: Añadir esto

// Configuración de pines I2S para PCM5102A
#define I2S_BCK_PIN 26
#define I2S_WS_PIN 25
#define I2S_DATA_PIN 22

// Pines táctiles
#define TOUCH_PIN_1 14
#define TOUCH_PIN_2 33

// Declaraciones forward
void stopAudio();
void startAudio();

// Variables globales
Preferences preferences;
WebServer server(80);
WiFiManager wifiManager;

// Objetos de audio
AudioGenerator *audioGen = nullptr; 
AudioFileSourceSPIFFS *file = nullptr;
AudioOutputI2S *out = nullptr;
AudioFileSourceID3 *id3 = nullptr;
AudioFileSourceBuffer *buff = nullptr; // <--- NUEVO: Buffer intermedio

int touchThreshold = 40;
bool isPlaying = false; // Indica si el audio debe avanzar
bool isLoaded = false;  // Indica si el archivo está listo en RAM
bool isFadingOut = false;
unsigned long timeAtZeroVol = 0; // Para controlar el tiempo de silencio
float currentVolume = 0.0f;
unsigned long lastFadeUpdate = 0;
size_t audioFileSize = 0;

// Variables de lectura táctil
int currentTouch1 = 0;
int currentTouch2 = 0;

const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Touch Music Config</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      min-height: 100vh;
      display: flex;
      align-items: center;
      justify-content: center;
      padding: 20px;
    }
    .container {
      background: white;
      border-radius: 20px;
      padding: 40px;
      max-width: 500px;
      width: 100%;
      box-shadow: 0 20px 60px rgba(0,0,0,0.3);
    }
    h1 {
      color: #667eea;
      margin-bottom: 10px;
      font-size: 28px;
    }
    .subtitle {
      color: #666;
      margin-bottom: 30px;
      font-size: 14px;
    }
    .card {
      background: #f8f9fa;
      border-radius: 12px;
      padding: 20px;
      margin-bottom: 20px;
    }
    .card h3 {
      color: #333;
      margin-bottom: 15px;
      font-size: 16px;
      display: flex;
      align-items: center;
    }
    .card h3::before {
      content: '👆';
      margin-right: 8px;
      font-size: 20px;
    }
    .reading {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 10px;
      padding: 12px;
      background: white;
      border-radius: 8px;
    }
    .reading:last-child { margin-bottom: 0; }
    .label {
      color: #666;
      font-weight: 500;
    }
    .value {
      font-size: 24px;
      font-weight: bold;
      color: #667eea;
      font-family: 'Courier New', monospace;
    }
    .config-card h3::before {
      content: '⚙️';
    }
    .audio-card h3::before {
      content: '🎵';
    }
    .form-group {
      margin-bottom: 20px;
    }
    .form-group label {
      display: block;
      margin-bottom: 8px;
      color: #333;
      font-weight: 500;
    }
    .input-group {
      display: flex;
      gap: 10px;
      align-items: center;
    }
    input[type="number"], input[type="file"] {
      flex: 1;
      padding: 12px 16px;
      border: 2px solid #e0e0e0;
      border-radius: 8px;
      font-size: 16px;
      transition: border 0.3s;
    }
    input[type="number"]:focus {
      outline: none;
      border-color: #667eea;
    }
    .current-value {
      background: #667eea;
      color: white;
      padding: 8px 16px;
      border-radius: 8px;
      font-weight: bold;
      min-width: 60px;
      text-align: center;
    }
    button {
      width: 100%;
      padding: 14px;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      color: white;
      border: none;
      border-radius: 8px;
      font-size: 16px;
      font-weight: 600;
      cursor: pointer;
      transition: transform 0.2s, box-shadow 0.2s;
      margin-top: 10px;
    }
    button:hover {
      transform: translateY(-2px);
      box-shadow: 0 4px 12px rgba(102, 126, 234, 0.4);
    }
    button:active {
      transform: translateY(0);
    }
    .status {
      margin-top: 15px;
      padding: 12px;
      border-radius: 8px;
      text-align: center;
      font-weight: 500;
      display: none;
    }
    .status.success {
      background: #d4edda;
      color: #155724;
      display: block;
    }
    .status.error {
      background: #f8d7da;
      color: #721c24;
      display: block;
    }
    .info {
      background: #e7f3ff;
      border-left: 4px solid #2196F3;
      padding: 12px;
      margin-top: 20px;
      border-radius: 4px;
      font-size: 13px;
      color: #333;
    }
    .audio-info {
      background: #fff3cd;
      border-left: 4px solid #ffc107;
      padding: 12px;
      margin-bottom: 15px;
      border-radius: 4px;
      font-size: 13px;
      color: #333;
    }
    .progress-bar {
      width: 100%;
      height: 20px;
      background: #e0e0e0;
      border-radius: 10px;
      overflow: hidden;
      margin-top: 10px;
      display: none;
    }
    .progress-fill {
      height: 100%;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      width: 0%;
      transition: width 0.3s;
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>🎵 Touch Music Player</h1>
    <p class="subtitle">Con ESP8266Audio Library</p>
    
    <div class="card audio-card">
      <h3>Audio File</h3>
      <div class="audio-info" id="audioInfo">
        Archivo: <strong id="fileName">No cargado</strong><br>
        Tamaño: <strong id="fileSize">-- KB</strong><br>
        Espacio libre: <strong id="freeSpace">-- KB</strong>
      </div>
      <button id="deleteBtn" style="background: #dc3545; display: none;" onclick="deleteAudio()">🗑️ Borrar Audio Actual</button>
      <form id="uploadForm" enctype="multipart/form-data">
        <input type="file" id="audioFile" name="audio" accept=".wav,.mp3" required>
        <button type="submit">📤 Subir Audio (.wav o .mp3)</button>
      </form>
      <div class="progress-bar" id="progressBar">
        <div class="progress-fill" id="progressFill"></div>
      </div>
      <div class="status" id="uploadStatus"></div>
    </div>

    <div class="card">
      <h3>Lecturas Táctiles</h3>
      <div class="reading">
        <span class="label">Touch Pin 1 (GPIO 4):</span>
        <span class="value" id="touch1">--</span>
      </div>
      <div class="reading">
        <span class="label">Touch Pin 2 (GPIO 15):</span>
        <span class="value" id="touch2">--</span>
      </div>
    </div>

    <div class="card config-card">
      <h3>Configuración</h3>
      <form id="configForm">
        <div class="form-group">
          <label>Umbral de Sensibilidad:</label>
          <div class="input-group">
            <input type="number" id="threshold" name="threshold" min="1" max="100" required>
            <div class="current-value" id="currentThreshold">--</div>
          </div>
        </div>
        <button type="submit">💾 Guardar Configuración</button>
      </form>
      <div class="status" id="status"></div>
    </div>

    <div class="info">
      💡 <strong>Tip:</strong> Sube tu audio en formato WAV (cualquier sample rate) o MP3. La librería ESP8266Audio se encargará de reproducirlo correctamente.
    </div>
  </div>

  <script>
    function updateReadings() {
      fetch('/readings')
        .then(r => r.json())
        .then(data => {
          document.getElementById('touch1').textContent = data.touch1;
          document.getElementById('touch2').textContent = data.touch2;
          document.getElementById('currentThreshold').textContent = data.threshold;
          document.getElementById('freeSpace').textContent = (data.freeSpace / 1024).toFixed(1) + ' KB';
          
          const deleteBtn = document.getElementById('deleteBtn');
          if (data.audioLoaded) {
            const fileName = data.audioFile || 'audio';
            document.getElementById('fileName').textContent = fileName;
            document.getElementById('fileSize').textContent = (data.audioSize / 1024).toFixed(1) + ' KB';
            deleteBtn.style.display = 'block';
          } else {
            document.getElementById('fileName').textContent = 'No cargado';
            document.getElementById('fileSize').textContent = '-- KB';
            deleteBtn.style.display = 'none';
          }
        })
        .catch(e => console.error('Error:', e));
    }

    async function deleteAudio() {
      if (!confirm('¿Seguro que quieres borrar el audio actual?')) return;
      
      const status = document.getElementById('uploadStatus');
      try {
        const response = await fetch('/delete', { method: 'POST' });
        if (response.ok) {
          status.className = 'status success';
          status.textContent = '✓ Audio borrado correctamente';
          setTimeout(() => {
            status.style.display = 'none';
            updateReadings();
          }, 2000);
        } else {
          throw new Error('Error al borrar');
        }
      } catch (error) {
        status.className = 'status error';
        status.textContent = '✗ Error al borrar el audio';
      }
    }

    document.getElementById('configForm').addEventListener('submit', async (e) => {
      e.preventDefault();
      const threshold = document.getElementById('threshold').value;
      const status = document.getElementById('status');
      
      try {
        const response = await fetch('/save', {
          method: 'POST',
          headers: {'Content-Type': 'application/x-www-form-urlencoded'},
          body: 'threshold=' + threshold
        });
        
        if (response.ok) {
          status.className = 'status success';
          status.textContent = '✓ Configuración guardada correctamente';
          setTimeout(() => status.style.display = 'none', 3000);
        } else {
          throw new Error('Error al guardar');
        }
      } catch (error) {
        status.className = 'status error';
        status.textContent = '✗ Error al guardar la configuración';
      }
    });

    document.getElementById('uploadForm').addEventListener('submit', async (e) => {
      e.preventDefault();
      const formData = new FormData();
      const fileInput = document.getElementById('audioFile');
      const file = fileInput.files[0];
      
      if (!file) return;
      
      formData.append('audio', file);
      
      const progressBar = document.getElementById('progressBar');
      const progressFill = document.getElementById('progressFill');
      const status = document.getElementById('uploadStatus');
      
      progressBar.style.display = 'block';
      status.style.display = 'none';
      
      try {
        const xhr = new XMLHttpRequest();
        
        xhr.upload.addEventListener('progress', (e) => {
          if (e.lengthComputable) {
            const percent = (e.loaded / e.total) * 100;
            progressFill.style.width = percent + '%';
          }
        });
        
        xhr.addEventListener('load', () => {
          progressBar.style.display = 'none';
          if (xhr.status === 200) {
            status.className = 'status success';
            status.textContent = '✓ Audio subido correctamente. Reiniciando...';
            setTimeout(() => location.reload(), 2000);
          } else {
            status.className = 'status error';
            status.textContent = '✗ Error al subir el audio';
          }
        });
        
        xhr.open('POST', '/upload');
        xhr.send(formData);
        
      } catch (error) {
        progressBar.style.display = 'none';
        status.className = 'status error';
        status.textContent = '✗ Error al subir el archivo';
      }
    });

    updateReadings();
    setInterval(updateReadings, 500);
  </script>
</body>
</html>
)rawliteral";


void loadAudioEngine();
void freeAudioMemory();

void handleRoot() {
  server.send(200, "text/html", HTML_PAGE);
}

void handleReadings() {
  String audioFileName = "";
  bool audioExists = false;
  size_t currentAudioSize = 0;
  
  // Buscar archivo de audio
  File root = SPIFFS.open("/");
  File filetmp = root.openNextFile();
  while (filetmp) {
    String name = String(filetmp.name());
    if (name.endsWith(".wav") || name.endsWith(".mp3") || name.endsWith(".raw")) {
      audioFileName = name;
      currentAudioSize = filetmp.size();
      audioExists = true;
      break;
    }
    filetmp = root.openNextFile();
  }


  
  String json = "{";
  json += "\"touch1\":" + String(currentTouch1) + ",";
  json += "\"touch2\":" + String(currentTouch2) + ",";
  json += "\"threshold\":" + String(touchThreshold) + ",";
  json += "\"audioLoaded\":" + String(audioExists ? "true" : "false") + ",";
  json += "\"audioFile\":\"" + audioFileName + "\",";
  json += "\"audioSize\":" + String(currentAudioSize) + ",";
  json += "\"freeSpace\":" + String(SPIFFS.totalBytes() - SPIFFS.usedBytes());
  json += "}";
  server.send(200, "application/json", json);
}

void handleSave() {
  if (server.hasArg("threshold")) {
    touchThreshold = server.arg("threshold").toInt();
    preferences.begin("touch-music", false);
    preferences.putInt("threshold", touchThreshold);
    preferences.end();
    Serial.printf("Nuevo umbral guardado: %d\n", touchThreshold);
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Missing parameter");
  }
}

void handleDelete() {
  Serial.println("Borrando archivos de audio...");
  
  freeAudioMemory();
  
  // Borrar cualquier archivo de audio
  File root = SPIFFS.open("/");
  File filetmp = root.openNextFile();
  while (filetmp) {
    String name = String(filetmp.name());
    String path = name;
    
    // Asegurar que empiece con /
    if (!path.startsWith("/")) {
      path = "/" + path;
    }
    
    if (name.endsWith(".wav") || name.endsWith(".mp3") || name.endsWith(".raw")) {
      filetmp.close();
      
      Serial.printf("Intentando borrar: %s\n", path.c_str());
      if (SPIFFS.remove(path)) {
        Serial.printf("✓ Borrado: %s\n", path.c_str());
      } else {
        Serial.printf("✗ Error al borrar: %s\n", path.c_str());
      }
    }
    filetmp = root.openNextFile();
  }
  
  audioFileSize = 0;
  
  server.send(200, "text/plain", "OK");
  Serial.println("Archivos borrados");
}

void handleUpload() {
  HTTPUpload& upload = server.upload();
  static File uploadFile;
  static String targetFile;
  
  if (upload.status == UPLOAD_FILE_START) {
    freeAudioMemory(); 
    String filename = upload.filename;
    Serial.printf("Upload Start: %s\n", filename.c_str());
    
    // Determinar extensión
    if (filename.endsWith(".wav") || filename.endsWith(".WAV")) {
      targetFile = "/audio.wav";
    } else if (filename.endsWith(".mp3") || filename.endsWith(".MP3")) {
      targetFile = "/audio.mp3";
    } else {
      Serial.println("Formato no soportado");
      return;
    }
    
    // Eliminar TODOS los archivos de audio anteriores
    Serial.println("Limpiando archivos antiguos...");
    File root = SPIFFS.open("/");
    File filetmp = root.openNextFile();
    while (filetmp) {
      String name = String(filetmp.name());
      String path = name;
      
      // Asegurar que empiece con /
      if (!path.startsWith("/")) {
        path = "/" + path;
      }
      
      if (name.endsWith(".wav") || name.endsWith(".mp3") || name.endsWith(".raw")) {
        filetmp.close();
        
        Serial.printf("Borrando: %s\n", path.c_str());
        if (SPIFFS.remove(path)) {
          Serial.printf("✓ Borrado: %s\n", path.c_str());
        } else {
          Serial.printf("✗ Error: %s\n", path.c_str());
        }
      }
      filetmp = root.openNextFile();
    }
    
    uploadFile = SPIFFS.open(targetFile, "w");
    if (!uploadFile) {
      Serial.println("Error al crear archivo");
      return;
    }
  } 
  else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      uploadFile.write(upload.buf, upload.currentSize);
    }
  } 
  else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
      Serial.printf("Upload End: %u bytes\n", upload.totalSize);
      
      File f = SPIFFS.open(targetFile, "r");
      if (f) {
        audioFileSize = f.size();
        f.close();
        Serial.printf("Espacio libre: %d bytes\n", SPIFFS.totalBytes() - SPIFFS.usedBytes());
      }
      loadAudioEngine(); 
      server.send(200, "text/plain", "OK");
    }
  }
}

void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/readings", handleReadings);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/delete", HTTP_POST, handleDelete);
  server.on("/upload", HTTP_POST, []() {
    server.send(200);
  }, handleUpload);
  
  server.begin();
  Serial.println("Servidor web iniciado");
}




void freeAudioMemory() {
  Serial.println("Liberando memoria de audio...");
  isPlaying = false;
  isLoaded = false;
  
  if (audioGen) { delete audioGen; audioGen = nullptr; }
  if (buff) { delete buff; buff = nullptr; }
  if (file) { delete file; file = nullptr; }
  // NO borramos 'out' para evitar errores de I2S
}

// ESTA ES LA CLAVE: Cargamos el audio PERO NO lo reproducimos aún
void loadAudioEngine() {
  freeAudioMemory(); // Limpiar lo anterior si existe

  String audioPath = "";
  File root = SPIFFS.open("/");
  File filetmp = root.openNextFile();
  while (filetmp) {
    String name = String(filetmp.name());
    if (name.endsWith(".wav") || name.endsWith(".mp3")) {
      audioPath = name;
      break;
    }
    filetmp = root.openNextFile();
  }
  
  if (audioPath == "") return;
  if (!audioPath.startsWith("/")) audioPath = "/" + audioPath;

  Serial.printf("Pre-cargando: %s\n", audioPath.c_str());

  // 1. Fuente
  file = new AudioFileSourceSPIFFS(audioPath.c_str());
  
  // 2. Buffer GRANDE (16KB) para evitar cortes
  // El buffer carga datos AHORA, antes de que toques nada.
  buff = new AudioFileSourceBuffer(file, 16384); 
  
  // 3. Generador
  if (audioPath.endsWith(".wav")) {
    audioGen = new AudioGeneratorWAV();
  } else {
    audioGen = new AudioGeneratorMP3();
  }

  if (out && audioGen && buff) {
    out->SetGain(0); // Volumen a 0 por seguridad
    audioGen->begin(buff, out);
    isLoaded = true;
    isPlaying = false; // Cargado pero pausado
    Serial.println("✓ Audio listo en espera (Standby)");
  }
}


void stopAudio() {
  if (audioGen) {
    if (audioGen->isRunning()) {
      audioGen->stop();
    }
    delete audioGen;
    audioGen = nullptr;
  }
  
  // Borrar el buffer antes que el archivo fuente
  if (buff) {
    delete buff;
    buff = nullptr;
  }
  
  if (file) {
    delete file;
    file = nullptr;
  }
  
  // No borramos 'out' (AudioOutputI2S) para evitar el error de registro I2S
  // Lo mantenemos vivo entre canciones.
}

void startAudio() {
  stopAudio(); // Limpia lo anterior
  
  String audioPath = "";
  
  // Buscar cualquier archivo de audio
  File root = SPIFFS.open("/");
  File filetmp = root.openNextFile();
  while (filetmp) {
    String name = String(filetmp.name());
    if (name.endsWith(".wav") || name.endsWith(".mp3")) {
      audioPath = name;
      break;
    }
    filetmp = root.openNextFile();
  }
  
  if (audioPath == "") {
    Serial.println("No hay archivo de audio");
    return;
  }
  if (!audioPath.startsWith("/")) {
    audioPath = "/" + audioPath;
  }
  Serial.printf("Reproduciendo: %s\n", audioPath.c_str());
  
  // 1. Abrir archivo
  file = new AudioFileSourceSPIFFS(audioPath.c_str());
  
  // 2. Crear Buffer (4096 bytes suele ser suficiente, sube a 8192 si sigue fallando)
  buff = new AudioFileSourceBuffer(file, 4096); 
  
  // 3. Seleccionar decodificador
  if (audioPath.endsWith(".wav")) {
    audioGen = new AudioGeneratorWAV();
  } else if (audioPath.endsWith(".mp3")) {
    audioGen = new AudioGeneratorMP3();
  }
  
  if (buff && audioGen && out) {
    currentVolume = 0.0f;
    out->SetGain(currentVolume);
    
    // IMPORTANTE: Ahora el generador usa 'buff', no 'file'
    audioGen->begin(buff, out); 
    
    Serial.println("Audio iniciado con Buffer");
  }

}

void setup() {
  Serial.begin(115200);
  Serial.println("\n🎵 Touch Music Player - ESP8266Audio");
  
  WiFi.setSleep(false);

  // SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("Error al montar SPIFFS");
    return;
  }
  
  Serial.println("✓ SPIFFS montado");
  
  // Verificar audio
  if (SPIFFS.exists("/audio.wav")) {
    File f = SPIFFS.open("/audio.wav", "r");
    audioFileSize = f.size();
    f.close();
    Serial.printf("✓ Audio WAV: %.1f KB\n", audioFileSize/1024.0);
  } else if (SPIFFS.exists("/audio.mp3")) {
    File f = SPIFFS.open("/audio.mp3", "r");
    audioFileSize = f.size();
    f.close();
    Serial.printf("✓ Audio MP3: %.1f KB\n", audioFileSize/1024.0);
  } else {
    Serial.println("⚠ No hay audio - sube desde la web");
  }
  
  // Preferences
  preferences.begin("touch-music", false);
  touchThreshold = preferences.getInt("threshold", 40);
  preferences.end();
  
  // WiFi
  wifiManager.setConfigPortalTimeout(180);
  if (!wifiManager.autoConnect("TouchMusicAP")) {
    Serial.println("Fallo WiFi. Reiniciando...");
    delay(3000);
    ESP.restart();
  }
  
  Serial.println("✓ WiFi conectado");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  
  // I2S Output
  out = new AudioOutputI2S();
  out->SetPinout(I2S_BCK_PIN, I2S_WS_PIN, I2S_DATA_PIN);
  out->SetGain(0.5);
  
  // Web server
  setupWebServer();
  
  // Touch
  pinMode(TOUCH_PIN_1, INPUT);
  pinMode(TOUCH_PIN_2, INPUT);

  loadAudioEngine();
  
  Serial.println("✓ Sistema listo");
  Serial.printf("Web: http://%s\n", WiFi.localIP().toString().c_str());
}

void loop() {
  server.handleClient();
  
  // Si no hay audio cargado, no hacemos nada
  if (!isLoaded || !audioGen) {
    delay(10);
    return;
  }

  currentTouch1 = touchRead(TOUCH_PIN_1);
  currentTouch2 = touchRead(TOUCH_PIN_2);
  bool touching = (currentTouch1 < touchThreshold && currentTouch2 < touchThreshold);

  // --- LÓGICA DE CONTROL ---
  
  if (touching) {
    if (!isPlaying) {
      Serial.println("Play >");
      isPlaying = true; 
      isFadingOut = false;
      timeAtZeroVol = 0; // Resetear timer
    }
    
    // Si volvemos a tocar mientras se apagaba, cancelamos el apagado
    if (isFadingOut) {
       isFadingOut = false;
       timeAtZeroVol = 0;
    }
    
    // Fade IN (Subida suave pero rápida)
    if (currentVolume < 1.0f) {
      currentVolume += 0.02f; 
      if (currentVolume > 1.0f) currentVolume = 1.0f;
      out->SetGain(currentVolume);
    }
  } 
  else {
    // Si soltamos, iniciamos Fade OUT
    if (isPlaying && !isFadingOut) {
      isFadingOut = true;
    }
  }

  // --- LÓGICA DE FADE OUT Y REINICIO ---
  if (isFadingOut) {
    // 1. Bajamos volumen (más lento para que sea más suave: 0.01 en vez de 0.03)
    if (currentVolume > 0.0f) {
      currentVolume -= 0.009f; 
      if (currentVolume < 0.0f) currentVolume = 0.0f;
      out->SetGain(currentVolume);
    }
    
    // 2. Si ya estamos en silencio (Volumen 0)
    if (currentVolume == 0.0f) {
      // Si es la primera vez que tocamos el 0, marcamos el tiempo
      if (timeAtZeroVol == 0) {
        timeAtZeroVol = millis();
      }
      
      // 3. ESPERAMOS un poco (ej. 150ms) enviando silencio al DAC
      // Esto "empuja" el audio antiguo fuera del buffer
      if (millis() - timeAtZeroVol > 150) {
        isPlaying = false;
        isFadingOut = false;
        timeAtZeroVol = 0;
        
        Serial.println("Stop & Reload <<");
        // Ahora sí es seguro matar el proceso y recargar
        loadAudioEngine(); 
      }
    }
  }

  // --- LOOP DE AUDIO ---
  // El loop debe seguir corriendo incluso durante los 150ms de silencio
  if (isPlaying && audioGen->isRunning()) {
    if (!audioGen->loop()) {
      Serial.println("Canción terminada. Recargando...");
      isPlaying = false;
      loadAudioEngine();
    }
  }
}
