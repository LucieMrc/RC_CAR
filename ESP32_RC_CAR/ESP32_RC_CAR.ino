#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <ESP32Servo.h>

// --- CONFIGURATION BLE ---
#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHAR_LEFT_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define CHAR_RIGHT_UUID "ceb5483e-36e1-4688-b7f5-ea07361b26a9"

// --- BROCHES DES SERVO-MOTEURS ---
const int leftPin = 5;   // Signal du moteur gauche
const int rightPin = 18;  // Signal du moteur droit

Servo leftServo;
Servo rightServo;

// --- CONFIGURATION PHYSIQUE DES MOTEURS ---
// Si votre robot recule au lieu d'avancer quand vous activez les deux moteurs,
// changez simplement cette constante (true ou false) pour adapter le sens de rotation.
const bool INVERT_RIGHT = true;

// --- CONSTANTES EN MICROSECONDES (Haute Précision) ---
const int STOP_VALUE = 1500;   // Point mort exact
const int FORWARD_MAX = 2100;  // Pleine vitesse Avant (logique)
const int REVERSE_MAX = 900;   // Pleine vitesse Arrière (logique)

// --- PARAMÈTRES DE LA PHYSIQUE (Rampe et Freinage Actif) ---
const int rampStep = 15;      // Inertie / Accélération progressive
const int brakeStep = 100;    // Freinage d'urgence (Inversion brutale)
const int rampInterval = 15;  // Temps (ms) entre chaque palier

const int JUMP_FWD = 1590;  // Enjamber la zone morte électrique vers l'avant
const int JUMP_REV = 1410;  // Enjamber la zone morte électrique vers l'arrière

// Variables de suivi indépendantes
int currentSpeedLeft = STOP_VALUE;
int targetSpeedLeft = STOP_VALUE;
unsigned long lastUpdateLeft = 0;

int currentSpeedRight = STOP_VALUE;
int targetSpeedRight = STOP_VALUE;
unsigned long lastUpdateRight = 0;


// --- CALLBACKS BLE : MOTEUR GAUCHE ---
class LeftCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    String value = pCharacteristic->getValue();
    if (value.length() > 0) {
      uint8_t receivedVal = (uint8_t)value[0];
      if (receivedVal == 1) {
        targetSpeedLeft = FORWARD_MAX;
        Serial.println("Gauche: AVANT");
      } else if (receivedVal == 2) {
        targetSpeedLeft = REVERSE_MAX;
        Serial.println("Gauche: ARRIÈRE");
      } else if (receivedVal == 0) {
        targetSpeedLeft = STOP_VALUE;
        Serial.println("Gauche: ARRÊT (Inertie)");
      }
    }
  }
};

// --- CALLBACKS BLE : MOTEUR DROIT ---
class RightCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    String value = pCharacteristic->getValue();
    if (value.length() > 0) {
      uint8_t receivedVal = (uint8_t)value[0];
      if (receivedVal == 1) {
        targetSpeedRight = FORWARD_MAX;
        Serial.println("Droit: AVANT");
      } else if (receivedVal == 2) {
        targetSpeedRight = REVERSE_MAX;
        Serial.println("Droit: ARRIÈRE");
      } else if (receivedVal == 0) {
        targetSpeedRight = STOP_VALUE;
        Serial.println("Droit: ARRÊT (Inertie)");
      }
    }
  }
};

void setup() {
  Serial.begin(115200);
  Serial.println("debut du setup");

  // --- INITIALISATION DU MOTEUR GAUCHE ---
  leftServo.setPeriodHertz(50);
  leftServo.attach(leftPin, 900, 2100);
  leftServo.writeMicroseconds(STOP_VALUE);

  // --- INITIALISATION DU MOTEUR DROIT ---
  rightServo.setPeriodHertz(50);
  rightServo.attach(rightPin, 900, 2100);
  rightServo.writeMicroseconds(STOP_VALUE);

  // --- INITIALISATION DU SERVEUR BLE ---
  BLEDevice::init("ESP32 BLE Trigger");
  BLEServer *pServer = BLEDevice::createServer();
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Caractéristique Moteur Gauche
  BLECharacteristic *pLeftChar = pService->createCharacteristic(
    CHAR_LEFT_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  pLeftChar->setCallbacks(new LeftCallbacks());

  // Caractéristique Moteur Droit
  BLECharacteristic *pRightChar = pService->createCharacteristic(
    CHAR_RIGHT_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  pRightChar->setCallbacks(new RightCallbacks());

  // Lancement du service
  pService->start();
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);

  BLEDevice::startAdvertising();
  Serial.println("Configuration Tank BLE prête !");
}

void loop() {
  unsigned long now = millis();

  // --- PHYSIQUE MOTEUR GAUCHE ---
  if (currentSpeedLeft != targetSpeedLeft) {
    if (now - lastUpdateLeft > rampInterval) {
      lastUpdateLeft = now;

      if (currentSpeedLeft < targetSpeedLeft) {
        if (currentSpeedLeft < STOP_VALUE && targetSpeedLeft > STOP_VALUE) {
          currentSpeedLeft += brakeStep;  // Freinage actif
          if (currentSpeedLeft > STOP_VALUE) currentSpeedLeft = STOP_VALUE;
        } else if (currentSpeedLeft == STOP_VALUE) {
          currentSpeedLeft = JUMP_FWD;  // Anti-jitter
        } else {
          currentSpeedLeft += rampStep;  // Accélération
        }
        if (currentSpeedLeft > targetSpeedLeft) currentSpeedLeft = targetSpeedLeft;
      } else if (currentSpeedLeft > targetSpeedLeft) {
        if (currentSpeedLeft > STOP_VALUE && targetSpeedLeft < STOP_VALUE) {
          currentSpeedLeft -= brakeStep;  // Freinage actif
          if (currentSpeedLeft < STOP_VALUE) currentSpeedLeft = STOP_VALUE;
        } else if (currentSpeedLeft == STOP_VALUE) {
          currentSpeedLeft = JUMP_REV;  // Anti-jitter
        } else {
          currentSpeedLeft -= rampStep;  // Accélération
        }
        if (currentSpeedLeft < targetSpeedLeft) currentSpeedLeft = targetSpeedLeft;
      }

      leftServo.writeMicroseconds(currentSpeedLeft);
    }
  }

  // --- PHYSIQUE MOTEUR DROIT ---
  if (currentSpeedRight != targetSpeedRight) {
    if (now - lastUpdateRight > rampInterval) {
      lastUpdateRight = now;

      if (currentSpeedRight < targetSpeedRight) {
        if (currentSpeedRight < STOP_VALUE && targetSpeedRight > STOP_VALUE) {
          currentSpeedRight += brakeStep;  // Freinage actif
          if (currentSpeedRight > STOP_VALUE) currentSpeedRight = STOP_VALUE;
        } else if (currentSpeedRight == STOP_VALUE) {
          currentSpeedRight = JUMP_FWD;  // Anti-jitter
        } else {
          currentSpeedRight += rampStep;  // Accélération
        }
        if (currentSpeedRight > targetSpeedRight) currentSpeedRight = targetSpeedRight;
      } else if (currentSpeedRight > targetSpeedRight) {
        if (currentSpeedRight > STOP_VALUE && targetSpeedRight < STOP_VALUE) {
          currentSpeedRight -= brakeStep;  // Freinage actif
          if (currentSpeedRight < STOP_VALUE) currentSpeedRight = STOP_VALUE;
        } else if (currentSpeedRight == STOP_VALUE) {
          currentSpeedRight = JUMP_REV;  // Anti-jitter
        } else {
          currentSpeedRight -= rampStep;  // Accélération
        }
        if (currentSpeedRight < targetSpeedRight) currentSpeedRight = targetSpeedRight;
      }

      // Application du signal matériel avec inversion automatique
      if (INVERT_RIGHT) {
        rightServo.writeMicroseconds(3000 - currentSpeedRight);
      } else {
        rightServo.writeMicroseconds(currentSpeedRight);
      }
    }
  }

  delay(2);  // Stabilité générale
}