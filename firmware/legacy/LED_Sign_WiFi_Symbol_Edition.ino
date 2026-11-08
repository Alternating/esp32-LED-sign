/*
 * STABLE Multi-Board LED Sign - Symbol Edition
 * ESP32 - WS2812B LED Matrix Display
 * 
 * NEW:
 * - Clickable board selection (no slider)
 * - Quick symbol buttons (❤, ★, ?, !, :), :( and arrows)
 * - ? and ! support
 * - Vertical smiley faces
 * 
 * Features:
 * - 1-8 board support via clickable boxes
 * - Smart word sequencing
 * - Symbol insertion
 * - Effects: Static, Blink, Fade, Color Cycle
 * - Dark mode web interface
 * - Zero flickering design
 * 
 * REQUIRED LIBRARY: FastLED
 */

#include <FastLED.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

// ===== LED CONFIGURATION =====
#define LEDS_PER_BOARD    400
#define MATRIX_WIDTH      20
#define MATRIX_HEIGHT     20
#define LED_TYPE          WS2812B
#define COLOR_ORDER       GRB
#define MAX_BOARDS        8

// ===== GPIO PIN ASSIGNMENTS =====
const uint8_t BOARD_PINS[MAX_BOARDS] = {
  5, 16, 17, 18, 19, 21, 22, 23
};

// ===== WIFI CONFIGURATION =====
const char* ssid = "LED_SIGN_AP";
const char* password = "sign1234";

// ===== LED ARRAYS =====
CRGB board1[LEDS_PER_BOARD];
CRGB board2[LEDS_PER_BOARD];
CRGB board3[LEDS_PER_BOARD];
CRGB board4[LEDS_PER_BOARD];
CRGB board5[LEDS_PER_BOARD];
CRGB board6[LEDS_PER_BOARD];
CRGB board7[LEDS_PER_BOARD];
CRGB board8[LEDS_PER_BOARD];

CRGB* boards[MAX_BOARDS] = {
  board1, board2, board3, board4,
  board5, board6, board7, board8
};

// ===== DISPLAY STATE =====
struct DisplayConfig {
  uint8_t numBoards;
  String message;
  CRGB color;
  uint8_t brightness;
  String effect;
  int effectSpeed;
  int wordSpeed;
};

DisplayConfig config;
DisplayConfig pendingConfig;
bool configChanged = false;

// ===== WORD SEQUENCING STATE =====
String wordSequence[20];
int wordSequenceCount = 0;
int currentWordIndex = 0;
unsigned long lastWordUpdate = 0;

// ===== EFFECT STATE =====
unsigned long lastEffectUpdate = 0;
uint8_t effectPhase = 0;
uint8_t hueOffset = 0;

// ===== WEB SERVER =====
WebServer server(80);

// ===== CHARACTER PATTERNS =====
#include "character_patterns.h"

// ===== SYMBOL CODE CONVERSION =====
// Convert symbol names to special character codes
char getSymbolCode(String symbol) {
  if (symbol == "heart") return (char)1;
  if (symbol == "star") return (char)2;
  if (symbol == "check") return (char)3;
  if (symbol == "cross") return (char)4;
  if (symbol == "right") return (char)5;
  if (symbol == "left") return (char)6;
  if (symbol == "up") return (char)7;
  if (symbol == "down") return (char)8;
  if (symbol == "diamond") return (char)9;
  if (symbol == "circle") return (char)10;
  if (symbol == "question") return '?';
  if (symbol == "exclaim") return '!';
  if (symbol == "happy") return (char)13;
  if (symbol == "sad") return (char)14;
  return ' ';
}

// ===== WORD PARSING =====
void parseMessageIntoWords() {
  wordSequenceCount = 0;
  currentWordIndex = 0;
  
  if (config.message.length() == 0) {
    wordSequence[0] = "A";
    wordSequenceCount = 1;
    return;
  }
  
  // Split message into words
  String words[50];
  int wordCount = 0;
  String currentWord = "";
  
  for (int i = 0; i < config.message.length(); i++) {
    char c = config.message.charAt(i);
    if (c == ' ') {
      if (currentWord.length() > 0) {
        words[wordCount++] = currentWord;
        currentWord = "";
      }
    } else {
      currentWord += c;
    }
  }
  if (currentWord.length() > 0) {
    words[wordCount++] = currentWord;
  }
  
  if (wordCount == 0) {
    wordSequence[0] = "A";
    wordSequenceCount = 1;
    return;
  }
  
  // Group words to fit board count
  int maxCharsPerGroup = config.numBoards;
  String currentGroup = "";
  
  for (int i = 0; i < wordCount; i++) {
    String word = words[i];
    
    if (word.length() > maxCharsPerGroup) {
      word = word.substring(0, maxCharsPerGroup);
    }
    
    String testGroup = currentGroup;
    if (testGroup.length() > 0) {
      testGroup += " ";
    }
    testGroup += word;
    
    if (testGroup.length() <= maxCharsPerGroup) {
      currentGroup = testGroup;
    } else {
      if (currentGroup.length() > 0) {
        wordSequence[wordSequenceCount++] = currentGroup;
      }
      currentGroup = word;
    }
  }
  
  if (currentGroup.length() > 0) {
    wordSequence[wordSequenceCount++] = currentGroup;
  }
  
  Serial.println("\n--- Word Sequence Parsed ---");
  Serial.print("Total groups: ");
  Serial.println(wordSequenceCount);
  for (int i = 0; i < wordSequenceCount; i++) {
    Serial.print("Group ");
    Serial.print(i + 1);
    Serial.print(": \"");
    Serial.print(wordSequence[i]);
    Serial.print("\" (");
    Serial.print(wordSequence[i].length());
    Serial.println(" chars)");
  }
  Serial.println("----------------------------\n");
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n================================");
  Serial.println("STABLE Multi-Board LED Sign");
  Serial.println("Symbol Edition");
  Serial.println("================================\n");
  
  config.numBoards = 1;
  config.message = "A";
  config.color = CRGB::Green;
  config.brightness = 25;
  config.effect = "static";
  config.effectSpeed = 500;
  config.wordSpeed = 2000;
  
  pendingConfig = config;
  
  FastLED.addLeds<LED_TYPE, 5, COLOR_ORDER>(board1, LEDS_PER_BOARD);
  FastLED.addLeds<LED_TYPE, 16, COLOR_ORDER>(board2, LEDS_PER_BOARD);
  FastLED.addLeds<LED_TYPE, 17, COLOR_ORDER>(board3, LEDS_PER_BOARD);
  FastLED.addLeds<LED_TYPE, 18, COLOR_ORDER>(board4, LEDS_PER_BOARD);
  FastLED.addLeds<LED_TYPE, 19, COLOR_ORDER>(board5, LEDS_PER_BOARD);
  FastLED.addLeds<LED_TYPE, 21, COLOR_ORDER>(board6, LEDS_PER_BOARD);
  FastLED.addLeds<LED_TYPE, 22, COLOR_ORDER>(board7, LEDS_PER_BOARD);
  FastLED.addLeds<LED_TYPE, 23, COLOR_ORDER>(board8, LEDS_PER_BOARD);
  
  FastLED.setBrightness(map(config.brightness, 0, 100, 0, 255));
  
  clearAllBoards();
  FastLED.show();
  
  Serial.println("FastLED initialized for 8 boards");
  
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);
  
  if (MDNS.begin("ledsign")) {
    Serial.println("mDNS responder started: http://ledsign.local");
  }
  
  server.on("/", handleRoot);
  server.on("/update", HTTP_POST, handleUpdate);
  server.on("/status", handleStatus);
  server.begin();
  
  Serial.println("Web server started");
  Serial.println("================================\n");
  
  parseMessageIntoWords();
  updateDisplay();
}

// ===== MAIN LOOP =====
void loop() {
  server.handleClient();
  
  if (configChanged) {
    config = pendingConfig;
    configChanged = false;
    
    FastLED.setBrightness(map(config.brightness, 0, 100, 0, 255));
    
    lastEffectUpdate = millis();
    lastWordUpdate = millis();
    effectPhase = 0;
    hueOffset = 0;
    
    parseMessageIntoWords();
    updateDisplay();
    
    Serial.println("Configuration updated");
  }
  
  if (wordSequenceCount > 1) {
    unsigned long now = millis();
    if (now - lastWordUpdate >= config.wordSpeed) {
      lastWordUpdate = now;
      currentWordIndex = (currentWordIndex + 1) % wordSequenceCount;
      updateDisplay();
      
      Serial.print("Displaying word group ");
      Serial.print(currentWordIndex + 1);
      Serial.print("/");
      Serial.print(wordSequenceCount);
      Serial.print(": \"");
      Serial.print(wordSequence[currentWordIndex]);
      Serial.println("\"");
    }
  }
  
  unsigned long now = millis();
  if (now - lastEffectUpdate >= config.effectSpeed) {
    lastEffectUpdate = now;
    
    if (config.effect == "blink") {
      effectPhase = !effectPhase;
      updateDisplay();
    }
    else if (config.effect == "fade") {
      effectPhase++;
      if (effectPhase > 100) effectPhase = 0;
      updateDisplay();
    }
    else if (config.effect == "cycle") {
      hueOffset += 5;
      if (hueOffset >= 255) hueOffset = 0;
      updateDisplay();
    }
  }
  
  delay(10);
}

// ===== CLEAR ALL BOARDS =====
void clearAllBoards() {
  for (int i = 0; i < MAX_BOARDS; i++) {
    fill_solid(boards[i], LEDS_PER_BOARD, CRGB::Black);
  }
}

// ===== UPDATE DISPLAY =====
void updateDisplay() {
  clearAllBoards();
  
  CRGB displayColor = config.color;
  
  if (config.effect == "blink") {
    if (effectPhase == 0) {
      displayColor = CRGB::Black;
    }
  }
  else if (config.effect == "fade") {
    uint8_t fadeBrightness = map(effectPhase, 0, 100, 77, 255);
    displayColor.nscale8(fadeBrightness);
  }
  else if (config.effect == "cycle") {
    displayColor = CHSV(hueOffset, 255, 255);
  }
  
  String currentText = wordSequence[currentWordIndex];
  
  int textLen = currentText.length();
  for (int boardIdx = 0; boardIdx < config.numBoards && boardIdx < textLen; boardIdx++) {
    char c = currentText.charAt(boardIdx);
    displayCharacterOnBoard(c, boardIdx, displayColor);
  }
  
  FastLED.show();
}

// ===== DISPLAY CHARACTER ON BOARD =====
void displayCharacterOnBoard(char c, uint8_t boardIndex, CRGB color) {
  if (boardIndex >= MAX_BOARDS) return;
  
  const bool* pattern = getCharacterPattern(c);
  if (pattern == nullptr) return;
  
  CRGB* board = boards[boardIndex];
  
  for (int row = 0; row < MATRIX_HEIGHT; row++) {
    for (int col = 0; col < MATRIX_WIDTH; col++) {
      int bitmapIndex = row * MATRIX_WIDTH + (MATRIX_WIDTH - 1 - col);
      
      int ledIndex;
      if (row % 2 == 0) {
        ledIndex = row * MATRIX_WIDTH + col;
      } else {
        ledIndex = row * MATRIX_WIDTH + (MATRIX_WIDTH - 1 - col);
      }
      
      if (pattern[bitmapIndex]) {
        board[ledIndex] = color;
      } else {
        board[ledIndex] = CRGB::Black;
      }
    }
  }
}

// ===== WEB HANDLERS =====
void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>LED Sign Control</title>
  <style>
    * {
      margin: 0;
      padding: 0;
      box-sizing: border-box;
    }
    
    body {
      font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
      background: #f5f5f5;
      color: #333;
      transition: all 0.3s;
    }
    
    body.dark-mode {
      background: #1a1a1a;
      color: #e0e0e0;
    }
    
    .container {
      max-width: 800px;
      margin: 20px auto;
      padding: 20px;
    }
    
    .header {
      background: white;
      padding: 20px;
      border-radius: 10px;
      box-shadow: 0 2px 10px rgba(0,0,0,0.1);
      margin-bottom: 20px;
      display: flex;
      justify-content: space-between;
      align-items: center;
    }
    
    body.dark-mode .header {
      background: #2d2d2d;
      box-shadow: 0 2px 10px rgba(0,0,0,0.3);
    }
    
    h1 {
      font-size: 24px;
      color: #333;
    }
    
    body.dark-mode h1 {
      color: #e0e0e0;
    }
    
    .theme-toggle {
      background: #007bff;
      color: white;
      border: none;
      padding: 10px 20px;
      border-radius: 5px;
      cursor: pointer;
      font-size: 14px;
    }
    
    .theme-toggle:hover {
      background: #0056b3;
    }
    
    .card {
      background: white;
      padding: 20px;
      border-radius: 10px;
      box-shadow: 0 2px 10px rgba(0,0,0,0.1);
      margin-bottom: 20px;
    }
    
    body.dark-mode .card {
      background: #2d2d2d;
      box-shadow: 0 2px 10px rgba(0,0,0,0.3);
    }
    
    .card h2 {
      font-size: 18px;
      margin-bottom: 15px;
      color: #007bff;
    }
    
    .form-group {
      margin-bottom: 20px;
    }
    
    label {
      display: block;
      margin-bottom: 8px;
      font-weight: 500;
    }
    
    input[type="text"],
    input[type="number"],
    select,
    textarea {
      width: 100%;
      padding: 10px;
      border: 1px solid #ddd;
      border-radius: 5px;
      font-size: 14px;
      background: white;
      color: #333;
      font-family: inherit;
    }
    
    textarea {
      min-height: 100px;
      resize: vertical;
    }
    
    body.dark-mode input[type="text"],
    body.dark-mode input[type="number"],
    body.dark-mode select,
    body.dark-mode textarea {
      background: #3d3d3d;
      border-color: #555;
      color: #e0e0e0;
    }
    
    input[type="range"] {
      width: 100%;
      margin: 10px 0;
    }
    
    input[type="color"] {
      width: 100%;
      height: 50px;
      border: none;
      border-radius: 5px;
      cursor: pointer;
    }
    
    .value-display {
      display: inline-block;
      background: #007bff;
      color: white;
      padding: 5px 15px;
      border-radius: 20px;
      font-size: 14px;
      margin-left: 10px;
    }
    
    .board-visual {
      display: flex;
      gap: 10px;
      margin: 15px 0;
      flex-wrap: wrap;
    }
    
    .board-box {
      width: 70px;
      height: 70px;
      border: 3px solid #ddd;
      border-radius: 8px;
      display: flex;
      align-items: center;
      justify-content: center;
      font-weight: bold;
      font-size: 24px;
      color: #999;
      transition: all 0.3s;
      cursor: pointer;
      user-select: none;
    }
    
    body.dark-mode .board-box {
      border-color: #555;
    }
    
    .board-box:hover {
      transform: scale(1.05);
      border-color: #007bff;
    }
    
    .board-box.active {
      border-color: #28a745;
      background: #d4edda;
      color: #28a745;
      box-shadow: 0 0 15px rgba(40, 167, 69, 0.3);
    }
    
    body.dark-mode .board-box.active {
      background: #1e4620;
      color: #4caf50;
    }
    
    .symbol-buttons {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(80px, 1fr));
      gap: 10px;
      margin-top: 15px;
    }
    
    .symbol-btn {
      padding: 12px;
      border: 2px solid #007bff;
      background: white;
      color: #007bff;
      border-radius: 8px;
      cursor: pointer;
      font-size: 24px;
      transition: all 0.2s;
      display: flex;
      align-items: center;
      justify-content: center;
      min-height: 60px;
    }
    
    body.dark-mode .symbol-btn {
      background: #2d2d2d;
      border-color: #64b5f6;
      color: #64b5f6;
    }
    
    .symbol-btn:hover {
      background: #007bff;
      color: white;
      transform: translateY(-2px);
      box-shadow: 0 4px 10px rgba(0, 123, 255, 0.3);
    }
    
    body.dark-mode .symbol-btn:hover {
      background: #64b5f6;
      color: #1a1a1a;
    }
    
    .info-box {
      background: #e3f2fd;
      border-left: 4px solid #007bff;
      padding: 12px;
      margin-top: 10px;
      border-radius: 4px;
      font-size: 14px;
    }
    
    body.dark-mode .info-box {
      background: #1e3a5f;
      border-left-color: #64b5f6;
    }
    
    .btn {
      background: #28a745;
      color: white;
      border: none;
      padding: 15px 30px;
      border-radius: 5px;
      font-size: 16px;
      font-weight: bold;
      cursor: pointer;
      width: 100%;
      transition: all 0.3s;
    }
    
    .btn:hover {
      background: #218838;
      transform: translateY(-2px);
      box-shadow: 0 4px 15px rgba(40, 167, 69, 0.4);
    }
    
    .status {
      background: #17a2b8;
      color: white;
      padding: 15px;
      border-radius: 5px;
      margin-top: 20px;
      display: none;
    }
    
    .status.show {
      display: block;
    }
  </style>
</head>
<body class="dark-mode">
  <div class="container">
    <div class="header">
      <h1>🎨 LED Sign Control</h1>
      <button class="theme-toggle" onclick="toggleTheme()">☀️ Light Mode</button>
    </div>
    
    <div class="card">
      <h2>Board Configuration</h2>
      <div class="form-group">
        <label>Select Number of Boards (Click boxes):</label>
        <div class="board-visual">
          <div class="board-box active" onclick="selectBoards(1)">1</div>
          <div class="board-box" onclick="selectBoards(2)">2</div>
          <div class="board-box" onclick="selectBoards(3)">3</div>
          <div class="board-box" onclick="selectBoards(4)">4</div>
          <div class="board-box" onclick="selectBoards(5)">5</div>
          <div class="board-box" onclick="selectBoards(6)">6</div>
          <div class="board-box" onclick="selectBoards(7)">7</div>
          <div class="board-box" onclick="selectBoards(8)">8</div>
        </div>
        <input type="hidden" id="numBoards" value="1">
      </div>
    </div>
    
    <div class="card">
      <h2>Quick Symbols</h2>
      <div class="symbol-buttons">
        <button class="symbol-btn" onclick="addSymbol('heart')" title="Heart">❤</button>
        <button class="symbol-btn" onclick="addSymbol('star')" title="Star">★</button>
        <button class="symbol-btn" onclick="addSymbol('question')" title="Question">?</button>
        <button class="symbol-btn" onclick="addSymbol('exclaim')" title="Exclamation">!</button>
        <button class="symbol-btn" onclick="addSymbol('happy')" title="Happy Face">:)</button>
        <button class="symbol-btn" onclick="addSymbol('sad')" title="Sad Face">:(</button>
        <button class="symbol-btn" onclick="addSymbol('up')" title="Arrow Up">↑</button>
        <button class="symbol-btn" onclick="addSymbol('down')" title="Arrow Down">↓</button>
        <button class="symbol-btn" onclick="addSymbol('left')" title="Arrow Left">←</button>
        <button class="symbol-btn" onclick="addSymbol('right')" title="Arrow Right">→</button>
      </div>
      <p style="margin-top: 15px; font-size: 13px; color: #666;">
        💡 Click where you want the symbol, then click a symbol button to insert it
      </p>
    </div>
    
    <div class="card">
      <h2>Message</h2>
      <div class="form-group">
        <label>Text:</label>
        <textarea id="message" placeholder="Enter your message">A</textarea>
        <div class="info-box">
          💡 <strong>Tips:</strong><br>
          • Click in text where you want a symbol, then click symbol button<br>
          • Words split intelligently based on board count<br>
          • Example: "hello ❤ world" → "hello" + "❤" + "world"
        </div>
      </div>
    </div>
    
    <div class="card">
      <h2>Timing</h2>
      <div class="form-group">
        <label>Word Change Speed: <span class="value-display" id="wordSpeedValue">2.0s</span></label>
        <input type="range" id="wordSpeed" min="500" max="5000" step="100" value="2000" oninput="updateWordSpeedDisplay()">
      </div>
    </div>
    
    <div class="card">
      <h2>Appearance</h2>
      <div class="form-group">
        <label>Color:</label>
        <input type="color" id="color" value="#00ff00">
      </div>
      
      <div class="form-group">
        <label>Brightness: <span class="value-display" id="brightnessValue">25%</span></label>
        <input type="range" id="brightness" min="0" max="100" value="25" oninput="updateBrightnessDisplay()">
      </div>
      
      <div class="form-group">
        <label>Effect:</label>
        <select id="effect" onchange="updateEffectSpeed()">
          <option value="static">Static</option>
          <option value="blink">Blink</option>
          <option value="fade">Fade</option>
          <option value="cycle">Color Cycle</option>
        </select>
      </div>
      
      <div class="form-group" id="speedControl" style="display:none;">
        <label>Effect Speed: <span class="value-display" id="speedValue">500ms</span></label>
        <input type="range" id="speed" min="100" max="2000" step="100" value="500" oninput="updateSpeedDisplay()">
      </div>
    </div>
    
    <button class="btn" onclick="updateSign()">🚀 Update Display</button>
    
    <div class="status" id="status">Configuration updated successfully!</div>
  </div>
  
  <script>
    let symbolMap = {
      'heart': '\x01',
      'star': '\x02',
      'check': '\x03',
      'cross': '\x04',
      'right': '\x05',
      'left': '\x06',
      'up': '\x07',
      'down': '\x08',
      'diamond': '\x09',
      'circle': '\x0A',
      'question': '?',
      'exclaim': '!',
      'happy': '\x0D',
      'sad': '\x0E'
    };
    
    function toggleTheme() {
      document.body.classList.toggle('dark-mode');
      const isDark = document.body.classList.contains('dark-mode');
      document.querySelector('.theme-toggle').textContent = isDark ? '☀️ Light Mode' : '🌙 Dark Mode';
      localStorage.setItem('theme', isDark ? 'dark' : 'light');
    }
    
    function initTheme() {
      const savedTheme = localStorage.getItem('theme');
      if (savedTheme === 'light') {
        document.body.classList.remove('dark-mode');
        document.querySelector('.theme-toggle').textContent = '🌙 Dark Mode';
      }
    }
    
    initTheme();
    
    function selectBoards(num) {
      document.getElementById('numBoards').value = num;
      const boxes = document.querySelectorAll('.board-box');
      boxes.forEach((box, index) => {
        if (index < num) {
          box.classList.add('active');
        } else {
          box.classList.remove('active');
        }
      });
    }
    
    function addSymbol(symbol) {
      const textarea = document.getElementById('message');
      const symbolChar = symbolMap[symbol];
      const startPos = textarea.selectionStart;
      const endPos = textarea.selectionEnd;
      const textBefore = textarea.value.substring(0, startPos);
      const textAfter = textarea.value.substring(endPos);
      
      textarea.value = textBefore + symbolChar + textAfter;
      textarea.selectionStart = textarea.selectionEnd = startPos + symbolChar.length;
      textarea.focus();
    }
    
    function updateBrightnessDisplay() {
      const value = document.getElementById('brightness').value;
      document.getElementById('brightnessValue').textContent = value + '%';
    }
    
    function updateWordSpeedDisplay() {
      const value = document.getElementById('wordSpeed').value;
      const seconds = (value / 1000).toFixed(1);
      document.getElementById('wordSpeedValue').textContent = seconds + 's';
    }
    
    function updateEffectSpeed() {
      const effect = document.getElementById('effect').value;
      const speedControl = document.getElementById('speedControl');
      
      if (effect === 'static') {
        speedControl.style.display = 'none';
      } else {
        speedControl.style.display = 'block';
      }
    }
    
    function updateSpeedDisplay() {
      const value = document.getElementById('speed').value;
      document.getElementById('speedValue').textContent = value + 'ms';
    }
    
    function updateSign() {
      const message = document.getElementById('message').value || 'A';
      const numBoards = document.getElementById('numBoards').value;
      const color = document.getElementById('color').value;
      const brightness = document.getElementById('brightness').value;
      const effect = document.getElementById('effect').value;
      const speed = document.getElementById('speed').value;
      const wordSpeed = document.getElementById('wordSpeed').value;
      
      const r = parseInt(color.substr(1,2), 16);
      const g = parseInt(color.substr(3,2), 16);
      const b = parseInt(color.substr(5,2), 16);
      
      const params = new URLSearchParams({
        message: message,
        numBoards: numBoards,
        r: r,
        g: g,
        b: b,
        brightness: brightness,
        effect: effect,
        speed: speed,
        wordSpeed: wordSpeed
      });
      
      fetch('/update', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: params
      })
      .then(response => response.text())
      .then(data => {
        const status = document.getElementById('status');
        status.classList.add('show');
        setTimeout(() => status.classList.remove('show'), 3000);
      });
    }
  </script>
</body>
</html>
)rawliteral";
  
  server.send(200, "text/html", html);
}

void handleUpdate() {
  if (server.hasArg("message")) pendingConfig.message = server.arg("message");
  if (server.hasArg("numBoards")) pendingConfig.numBoards = server.arg("numBoards").toInt();
  if (server.hasArg("r") && server.hasArg("g") && server.hasArg("b")) {
    pendingConfig.color = CRGB(
      server.arg("r").toInt(),
      server.arg("g").toInt(),
      server.arg("b").toInt()
    );
  }
  if (server.hasArg("brightness")) pendingConfig.brightness = server.arg("brightness").toInt();
  if (server.hasArg("effect")) pendingConfig.effect = server.arg("effect");
  if (server.hasArg("speed")) pendingConfig.effectSpeed = server.arg("speed").toInt();
  if (server.hasArg("wordSpeed")) pendingConfig.wordSpeed = server.arg("wordSpeed").toInt();
  
  configChanged = true;
  
  Serial.println("\n--- Configuration Change Requested ---");
  Serial.print("Boards: "); Serial.println(pendingConfig.numBoards);
  Serial.print("Message: "); Serial.println(pendingConfig.message);
  Serial.print("Word Speed: "); Serial.print(pendingConfig.wordSpeed); Serial.println("ms");
  Serial.println("--------------------------------------\n");
  
  server.send(200, "text/plain", "OK");
}

void handleStatus() {
  String json = "{";
  json += "\"numBoards\":" + String(config.numBoards) + ",";
  json += "\"message\":\"" + config.message + "\",";
  json += "\"brightness\":" + String(config.brightness) + ",";
  json += "\"effect\":\"" + config.effect + "\",";
  json += "\"wordSpeed\":" + String(config.wordSpeed);
  json += "}";
  server.send(200, "application/json", json);
}
