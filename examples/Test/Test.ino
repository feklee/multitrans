#include "MultiTrans.h"
#include "MemoryFree.h"

#include "settings.h"

static const uint8_t ledPinNumber = 13;
static const uint8_t communicationPinNumber1 = 2;
static const uint8_t communicationPinNumber2 = 3;
static const uint8_t communicationPinNumber3 = 8;
static const uint8_t communicationPinNumber4 = 9;
static const uint8_t identificationPinNumber = 10;

using MT = MultiTrans<bitDurationExp,
                      maxNumberOfCharsPerTransmission,
                      recordDebugData>;
MT multiTransceiver;
MT::Transceiver<communicationPinNumber1> transceiver1;
MT::Transceiver<communicationPinNumber2> transceiver2;
MT::Transceiver<communicationPinNumber3> transceiver3;
MT::Transceiver<communicationPinNumber4> transceiver4;

static const uint32_t timeForOtherArduinoToStartUp = 1000; // ms

static char set[maxNumberOfCharsPerTransmission + 1]; // set of characters to
                                                      // transmit

ISR(TIMER2_COMPA_vect) {
  transceiver1.handleTimer2Interrupt();
  transceiver2.handleTimer2Interrupt();
  transceiver3.handleTimer2Interrupt();
  transceiver4.handleTimer2Interrupt();
}

ISR(PCINT2_vect) { // D0-D7
  transceiver1.handlePinChangeInterrupt();
  transceiver2.handlePinChangeInterrupt();
}

ISR(PCINT0_vect) { // D8-D13
  transceiver3.handlePinChangeInterrupt();
  transceiver4.handlePinChangeInterrupt();
}

void enablePinChangeInterrupts() {
  PCICR |= // Pin Change Interrupt Control Register
    bit(PCIE2) | // D0 to D7
    bit(PCIE0); // D8 to D15
}

bool randomBool() {
  return rand() % 2;
}

void flashLed(uint16_t duration = 100) {
  digitalWrite(ledPinNumber, HIGH);
  delay(duration);
  digitalWrite(ledPinNumber, LOW);
}

bool thisIsTheArduinoWithAnAsterisk() {
  pinMode(identificationPinNumber, INPUT_PULLUP);
  return !digitalRead(identificationPinNumber);
}

bool thisArduinoHasToWaitForSync() {
  return !thisIsTheArduinoWithAnAsterisk();
}

void loadSet(const uint8_t i) {
  strcpy_P(set, (char*)pgm_read_word(&(setsOfCharacters[i])));
}

template <typename T>
char firstReceivedCharacter(char character = 0) {
  static char recordedCharacter = 0;
  if (character != 0) {
    recordedCharacter = character;
  }
  return recordedCharacter;
}

template <typename T>
char lastReceivedCharacter(char character = 0) {
  static char recordedCharacter = 0;
  if (character != 0) {
    recordedCharacter = character;
  }
  return recordedCharacter;
}

template <typename T>
void errorRatioRecorderPrint(uint32_t errorsCounted,
                             uint32_t charactersCounted) {
  float ratio = charactersCounted == 0
    ? 0
    : float(errorsCounted) / charactersCounted;

  Serial.print(F("Error ratio w/o noise: "));
  Serial.print(ratio);
  Serial.print(F(" = "));
  Serial.print(errorsCounted);
  Serial.print(F(" / "));
  Serial.print(charactersCounted);
}

bool getCharacterPosition(const char character,
                          uint8_t &setNumber,
                          uint8_t &positionInSet) {
  for (setNumber = 0; setNumber < numberOfSets; setNumber ++) {
    loadSet(setNumber);
    const char *characterLocation = strchr(set, character);
    if (characterLocation != NULL) {
      positionInSet = (uint8_t)(characterLocation - set);
      return true;
    }
  }
  setNumber = 0;
  positionInSet = 0;
  return false;
}

// Returns the next expected character, or the specified optional character,
// which also syncs the position.
template <typename T>
char nextExpectedCharacter(char characterToSyncTo = 0) {
  static uint8_t setNumber = 0;
  static uint8_t positionInSet = 0;

  bool positionNeedsToBeSynced = characterToSyncTo != 0;
  if (positionNeedsToBeSynced) {
    bool gotCharacterPosition = getCharacterPosition(characterToSyncTo,
                                                     setNumber, positionInSet);
    if (!gotCharacterPosition) {
      return 0;
    }
  } else {
    loadSet(setNumber);
    positionInSet ++;
    if (positionInSet >= strlen(set)) {
      setNumber = (setNumber + 1) % numberOfSets;
      positionInSet = 0;
      loadSet(setNumber);
    }
  }

  return set[positionInSet];
}

template <typename T>
void errorRatioRecorderUpdate(uint32_t &errorsCounted,
                              uint32_t &charactersCounted,
                              char character) {
  static bool initialSyncIsNeeded = true;

  charactersCounted ++;

  if (initialSyncIsNeeded) {
    nextExpectedCharacter<T>(character);
    initialSyncIsNeeded = false;
    return;
  }

  bool characterIsUnexpected = character != nextExpectedCharacter<T>();
  if (characterIsUnexpected && charactersCounted > 1) {
    errorsCounted ++;
    nextExpectedCharacter<T>(character);
  }
}

template <typename T>
void errorRatioRecorder(char character, bool print) {
  static uint32_t errorsCounted = 0;
  static uint32_t charactersCounted = 0;

  if (print) {
    errorRatioRecorderPrint<T>(errorsCounted, charactersCounted);
  } else {
    errorRatioRecorderUpdate<T>(errorsCounted, charactersCounted, character);
  }
}

template <typename T>
void updateErrorRatio(char character) {
  errorRatioRecorder<T>(character, false);
}

template <typename T>
void printErrorRatio() {
  errorRatioRecorder<T>(0, true);
}

void pullUpCommunicationPins() {
  pinMode(communicationPinNumber1, INPUT_PULLUP);
  pinMode(communicationPinNumber2, INPUT_PULLUP);
  pinMode(communicationPinNumber3, INPUT_PULLUP);
  pinMode(communicationPinNumber4, INPUT_PULLUP);
}

void indicateSyncDone() {
  Serial.println(F("done"));
  flashLed(50);
  delay(50);
  flashLed(50);
  delay(50);
  flashLed(50);
}

void waitForSync() {
  pullUpCommunicationPins();
  delay(timeForOtherArduinoToStartUp);

  Serial.print(F("Waiting for sync..."));
  pinMode(communicationPinNumber1, INPUT_PULLUP);
  pinMode(communicationPinNumber2, INPUT_PULLUP);
  pinMode(communicationPinNumber3, INPUT_PULLUP);
  pinMode(communicationPinNumber4, INPUT_PULLUP);
  while (
    digitalRead(communicationPinNumber1) == HIGH &&
    digitalRead(communicationPinNumber2) == HIGH &&
    digitalRead(communicationPinNumber3) == HIGH &&
    digitalRead(communicationPinNumber4) == HIGH
  ) {}
  indicateSyncDone();
}

void initiateSync() {
  pullUpCommunicationPins();
  delay(timeForOtherArduinoToStartUp);
  delay(1000);

  Serial.print(F("Initiating sync..."));
  pinMode(communicationPinNumber1, OUTPUT);
  digitalWrite(communicationPinNumber1, LOW);
  pinMode(communicationPinNumber2, OUTPUT);
  digitalWrite(communicationPinNumber2, LOW);
  pinMode(communicationPinNumber3, OUTPUT);
  digitalWrite(communicationPinNumber3, LOW);
  pinMode(communicationPinNumber4, OUTPUT);
  digitalWrite(communicationPinNumber4, LOW);
  indicateSyncDone();
}

// E.g. in over current situations, the Arduino may restart. Flashing the LED
// shows when a restart is happening.
void indicateStartup() {
  flashLed();
  delay(100);
  flashLed();
}

void printStartupInformation() {
  printMemoryUsage(); // Initial use interesting in case of crash

  Serial.print(F("Test duration after initial sync: "));
  Serial.print(durationOfTest);
  Serial.println(F(" ms"));
}

void setup() {
  pinMode(ledPinNumber, OUTPUT);
  indicateStartup();

  Serial.begin(9600);
  printStartupInformation();

  if (arduinosShouldBeSynchronized) {
    if (thisArduinoHasToWaitForSync()) {
      waitForSync();
    } else {
      initiateSync();
    }
  }

  multiTransceiver.startTimer1();
  multiTransceiver.startTimer2();
  enablePinChangeInterrupts();

  transceiver1.begin();
  transceiver2.begin();
  transceiver3.begin();
  transceiver4.begin();

  uint32_t timeToWaitBeforeInitialTransmit = 10; // ms
  delay(timeToWaitBeforeInitialTransmit);
}

template <typename T>
uint32_t countTransmittedCharacters(uint8_t numberOfNewCharacters = 0) {
  static uint32_t numberOfCharacters = 0;
  numberOfCharacters += numberOfNewCharacters;
  return numberOfCharacters;
}

template <typename T>
void transmitNextSet(T &transceiver) {
  static uint8_t i = 0;

  loadSet(i);
  if (true/*TODO: !quiet*/) {
/*TODO:    flashLed();
    printPinNumberPrefix(transceiver);
    Serial.print(F("Starting transmission of: "));
    Serial.println(set);
*/
//    delay(100); // TODO: remove (without delay: 0 / 12, transmitted: 33, number of collisions: 183)
  }
  countTransmittedCharacters<T>(strlen(set));
  transceiver.startTransmissionOfCharacters(set);
  i = (i + 1) % numberOfSets;
}

const char *stringFromBinary(uint8_t x) {
  static char s[9];
  uint8_t i = 0;
  for (i = 0; i < 7; i ++) {
    uint8_t j = 7 - i;
    s[i] = (x & (1 << j)) ? '1' : '0';
  }
  s[i] = '\0';
  return s;
}

const char *stringFromBufferDimensions(
  uint8_t receiveBufferStart,
  uint8_t receiveBufferEnd
) {
  static char s[11];
  sprintf(s, "[%3d, %3d]", receiveBufferStart, receiveBufferEnd);
  return s;
}

void printDataRate() {
  Serial.print(F("Data rate: "));
  Serial.print(multiTransceiver.baudRate);
  Serial.print(F(" baud, effective data rate: "));
  Serial.print(multiTransceiver.effectiveDataRate);
  Serial.println(F(" bit/s"));
  Serial.print(F("Bit duration exponent for transmitter: "));
  Serial.print(multiTransceiver.tPrescaleFactorExp);
  Serial.print(F(" + "));
  Serial.print(multiTransceiver.tUnscaledBitDurationExp);
  Serial.print(F(", for receiver: "));
  Serial.print(multiTransceiver.rPrescaleFactorExp);
  Serial.print(F(" + "));
  Serial.println(multiTransceiver.rUnscaledBitDurationExp);
}

void printMemoryUsage() {
  Serial.print(F("Free memory: "));
  Serial.println(freeMemory());
}

void printGeneralInfo() {
  printDataRate();
  printMemoryUsage();
}

void printGeneralInfoFromTimeToTime() {
  static unsigned long lastTime = millis(); // ms
  unsigned long elapsedTime = millis() - lastTime; // ms
  if (elapsedTime > 2000) {
    printGeneralInfo();
    lastTime = millis();
  }
}

template <typename T>
void printInformationAboutCharacter(T &transceiver, char character) {
  Serial.print(character);
  Serial.print(F(" = "));
  Serial.print(stringFromBinary(character));
  Serial.print(F(" "));
  printErrorRatio<T>();
  Serial.println();

  if (multiTransceiver.debugDataIsRecorded) {
    reportCollisions(transceiver);
    reportReceiveBufferOverflows(transceiver);
  }
}

template <typename T>
void printInfoAboutNoise(T &transceiver) {
  if (transceiver.noiseWhileGettingCharacter()) {
    Serial.print(F("Noise has been detected on pin "));
    Serial.println(transceiver.pinNumber);
  }
}

template <typename T>
void printPinNumberPrefix(T &transceiver) {
  Serial.print(F("Pin "));
  Serial.print(transceiver.pinNumber);
  Serial.print(F(": "));
}

template <typename T>
void printReport(T &transceiver, char character) {
  uint8_t receiveBufferStart = transceiver.debugData.receiveBufferStart;
  uint8_t receiveBufferEnd = transceiver.debugData.receiveBufferEnd;

  printInfoAboutNoise(transceiver);
  printPinNumberPrefix(transceiver);
  if (multiTransceiver.debugDataIsRecorded) {
    Serial.print(stringFromBufferDimensions(receiveBufferStart,
                                            receiveBufferEnd));
    Serial.print(F(": "));
  }
  printInformationAboutCharacter<T>(transceiver, character);
}

template <typename T>
bool processNextCharacter(T &transceiver) {
  static bool firstReceivedCharacterRecorded = false;
  char character = transceiver.getNextCharacter();
  bool characterWasFound = character != 0;

  if (!characterWasFound) {
    return characterWasFound;
  }

  if (!firstReceivedCharacterRecorded) {
    firstReceivedCharacter<T>(character);
    firstReceivedCharacterRecorded = true;
  }
  lastReceivedCharacter<T>(character);

  updateErrorRatio<T>(character);
  if (!quiet) {
    printReport<T>(transceiver, character);
  }

  return characterWasFound;
}

template <typename T>
void transmitNoise(T &transceiver) {
  printPinNumberPrefix(transceiver);
  Serial.print(F("Starting transmission of noise."));
  transceiver.startTransmissionOfNoise();
}

template <typename T>
void transmitSomething(T &transceiver) {
  const bool noiseShouldBeInserted =
    noiseShouldBeRandomlyInserted ? randomBool() : false;

  if (noiseShouldBeInserted) {
    transmitNoise(transceiver);
  } else {
    transmitNextSet(transceiver);
  }
}

template <typename T>
void transmitIfPossible(T &transceiver) {
  if (!transceiver.transmissionIsInProgress()) {
    transmitSomething(transceiver);
  }
}

void transmitWherePossible() {
  transmitIfPossible(transceiver1);
  transmitIfPossible(transceiver2);
  transmitIfPossible(transceiver3);
  transmitIfPossible(transceiver4);
}

template <typename T>
void reportCollisions(T &transceiver) {
  static uint16_t oldNumberOfCollisions = 0;
  uint16_t numberOfCollisions = transceiver.debugData.numberOfCollisions;

  if (numberOfCollisions == oldNumberOfCollisions) {
    return;
  }

  printPinNumberPrefix(transceiver);
  Serial.print(F("Number of collisions increased to: "));
  Serial.println(numberOfCollisions);
  oldNumberOfCollisions = numberOfCollisions;
}

template <typename T>
void reportReceiveBufferOverflows(T &transceiver) {
  static uint16_t oldReceiveBufferOverflowCount = 0;
  uint16_t receiveBufferOverflowCount =
    transceiver.debugData.receiveBufferOverflowCount;

  if (receiveBufferOverflowCount == oldReceiveBufferOverflowCount) {
    return;
  }
  printPinNumberPrefix(transceiver);
  Serial.print(F("Number of receive buffer overflows increased to: "));
  Serial.println(receiveBufferOverflowCount);
  oldReceiveBufferOverflowCount = receiveBufferOverflowCount;
}

void processReceivedCharacters() {
  bool characterWasFound;
  do {
    bool characterWasFound1 = processNextCharacter(transceiver1);
    bool characterWasFound2 = processNextCharacter(transceiver2);
    bool characterWasFound3 = processNextCharacter(transceiver3);
    bool characterWasFound4 = processNextCharacter(transceiver4);
    characterWasFound = (characterWasFound1 ||
                         characterWasFound2 ||
                         characterWasFound3 ||
                         characterWasFound4);
  } while(characterWasFound);
}

template <typename T>
void printTestSummary(T &transceiver) {
  Serial.print(F("Pin "));
  Serial.print(transceiver.pinNumber);
  Serial.println(":");

  Serial.print("  ");
  printErrorRatio<T>();
  Serial.println();

  Serial.print(F("  Number of transmitted characters: "));
  Serial.println(countTransmittedCharacters<T>());

  Serial.print(F("  Number of collisions: "));
  Serial.println(transceiver.debugData.numberOfCollisions);

  Serial.print(F("  Number of receive buffer overflows: "));
  Serial.println(transceiver.debugData.receiveBufferOverflowCount);

  Serial.print(F("  Number of elements in receive buffer: "));
  Serial.println(transceiver.debugData.numberOfElementsInReceiveBuffer);

  Serial.print(F("  First received character: "));
  Serial.print(firstReceivedCharacter<T>());
  Serial.print(F(" = "));
  Serial.println(stringFromBinary(firstReceivedCharacter<T>()));

  Serial.print(F("  Last received character: "));
  Serial.print(lastReceivedCharacter<T>());
  Serial.print(F(" = "));
  Serial.println(stringFromBinary(lastReceivedCharacter<T>()));
}

void printTestSummary() {
  printTestSummary(transceiver1);
  printTestSummary(transceiver2);
  printTestSummary(transceiver3);
  printTestSummary(transceiver4);
}

void loop() {
  static bool testIsRunning = true;

  if (!testIsRunning) {
    return;
  }

  bool testHasEnded = millis() > durationOfTest;
  if (testHasEnded) {
    Serial.println("Test has ended!");
    printGeneralInfo();
    printTestSummary();
    testIsRunning = false;
    return;
  }

  if (!quiet) {
    printGeneralInfoFromTimeToTime();
  }

  transmitWherePossible();

  uint32_t endOfMinimumDelay = millis() + durationOfMinimumDelay;
  do {
    processReceivedCharacters();
  } while (millis() < endOfMinimumDelay);
}
