#define STBY 4

#define PWMA 5
#define AIN1 7
#define AIN2 8

#define PWMB 6
#define BIN1 9
#define BIN2 10

#define TRIG_PIN 2
#define ECHO_PIN 3

#define S1 A0
#define S2 A1
#define S3 A2
#define S4 A3
#define S5 A4

// Ic serit, aracin ilerleme yonune gore soldaysa true.
// Ic serit sagdaysa false yap.
const bool INNER_LANE_IS_LEFT = true;

// =======================
// HIZ AYARLARI
// =======================

int forwardSpeed = 75;

int softFastSpeed = 120;
int softSlowSpeed = 60;

int hardFastSpeed = 255;
int hardSlowSpeed = 35;

int searchSpeed = 160;

int laneChangeFastSpeed = 255;
int laneChangeSlowSpeed = 20;

// =======================
// ENGEL AYARLARI
// =======================

int obstacleCm = 25;
int obstacleConfirmCount = 2;
int obstacleCounter = 0;
int lastDistanceCm = 999;

unsigned long lastDistanceReadTime = 0;
const unsigned long distanceReadInterval = 60;

// =======================
// SERIT DEGISTIRME SURELERI
// =======================

const unsigned long laneLeaveMs = 650;
const unsigned long laneIgnoreLineMs = 350;
const unsigned long laneFindTimeoutMs = 2800;
const unsigned long laneAlignMs = 700;
const unsigned long obstacleCooldownMs = 1800;

// =======================
// STATE MACHINE
// =======================

enum CarState {
  FOLLOW_LINE,
  LANE_CHANGE_LEAVE,
  LANE_CHANGE_FIND,
  LANE_CHANGE_ALIGN
};

enum Lane {
  OUTER_LANE,
  INNER_LANE
};

CarState state = FOLLOW_LINE;
Lane currentLane = OUTER_LANE;
Lane targetLane = INNER_LANE;

unsigned long stateStartTime = 0;
unsigned long obstacleCooldownUntil = 0;

int lastDirection = 0; // -1 sol, 1 sag, 0 bilinmiyor
int laneChangeDir = 0; // -1 sola, 1 saga
unsigned long resumeSearchUntil = 0;

int s1, s2, s3, s4, s5;

bool cameraStopActive = false;

const char* actionText = "START";
const char* commandText = "NONE";

unsigned long lastDebugTime = 0;
const unsigned long debugInterval = 150;
const unsigned long resumeSearchMs = 1200;
const int resumeSearchSpeed = 70;

void setup()
{
  Serial.begin(9600);
  delay(1000);
  Serial.println("BOOT: AUTONOMOUSCAR-3 FACE STOP + ULTRASONIC LANE CHANGE");

  pinMode(STBY, OUTPUT);

  pinMode(PWMA, OUTPUT);
  pinMode(AIN1, OUTPUT);
  pinMode(AIN2, OUTPUT);

  pinMode(PWMB, OUTPUT);
  pinMode(BIN1, OUTPUT);
  pinMode(BIN2, OUTPUT);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  pinMode(S1, INPUT);
  pinMode(S2, INPUT);
  pinMode(S3, INPUT);
  pinMode(S4, INPUT);
  pinMode(S5, INPUT);

  digitalWrite(STBY, HIGH);
  stopMotors();
}

void loop()
{
  readSerialCommands();
  readLineSensors();
  updateDistance();

  if (cameraStopActive)
  {
    actionText = "CAMERA_STOP_PERSON";
    stopMotors();
    printDebug();
    return;
  }

  switch (state)
  {
    case FOLLOW_LINE:
      if (obstacleDetected() && millis() > obstacleCooldownUntil)
      {
        commandText = "ULTRASONIC";
        startLaneChange();
      }
      else
      {
        followLine();
      }
      break;

    case LANE_CHANGE_LEAVE:
      doLaneChangeTurn();

      if (millis() - stateStartTime >= laneLeaveMs)
      {
        state = LANE_CHANGE_FIND;
        stateStartTime = millis();
      }
      break;

    case LANE_CHANGE_FIND:
      doLaneChangeTurn();

      if ((millis() - stateStartTime >= laneIgnoreLineMs) && lineVisible())
      {
        currentLane = targetLane;
        state = LANE_CHANGE_ALIGN;
        stateStartTime = millis();
      }

      if (millis() - stateStartTime >= laneFindTimeoutMs)
      {
        currentLane = targetLane;
        state = LANE_CHANGE_ALIGN;
        stateStartTime = millis();
      }
      break;

    case LANE_CHANGE_ALIGN:
      followLine();

      if (millis() - stateStartTime >= laneAlignMs)
      {
        state = FOLLOW_LINE;
        obstacleCounter = 0;
        obstacleCooldownUntil = millis() + obstacleCooldownMs;
      }
      break;
  }

  printDebug();
}

// =======================
// RASPBERRY PI SERIAL
// =======================

void readSerialCommands()
{
  if (Serial.available() > 0)
  {
    String command = Serial.readStringUntil('\n');
    command.trim();

    if (command.length() > 0)
    {
      Serial.print("RECEIVED:");
      Serial.println(command);

      if (command == "STOP_HUMAN" || command == "STOP" || command == "PERSON")
      {
        cameraStopActive = true;
        commandText = "STOP_HUMAN";
        stopMotors();
      }
      else if (command == "FOLLOW" || command == "CLEAR" || command == "RESUME")
      {
        cameraStopActive = false;
        commandText = "FOLLOW";
        state = FOLLOW_LINE;
        obstacleCounter = 0;
        obstacleCooldownUntil = millis() + 700;
        resumeSearchUntil = millis() + resumeSearchMs;
        digitalWrite(STBY, HIGH);
      }
    }
  }
}

// =======================
// SENSOR OKUMA
// =======================

void readLineSensors()
{
  s1 = digitalRead(S1);
  s2 = digitalRead(S2);
  s3 = digitalRead(S3);
  s4 = digitalRead(S4);
  s5 = digitalRead(S5);
}

bool lineVisible()
{
  return (s1 == 0 || s2 == 0 || s3 == 0 || s4 == 0 || s5 == 0);
}

void updateDistance()
{
  if (millis() - lastDistanceReadTime < distanceReadInterval)
  {
    return;
  }

  lastDistanceReadTime = millis();
  lastDistanceCm = readDistanceCm();

  if (lastDistanceCm > 0 && lastDistanceCm <= obstacleCm)
  {
    if (obstacleCounter < obstacleConfirmCount)
    {
      obstacleCounter++;
    }
  }
  else
  {
    obstacleCounter = 0;
  }
}

int readDistanceCm()
{
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long duration = pulseIn(ECHO_PIN, HIGH, 25000);

  if (duration == 0)
  {
    return 999;
  }

  return duration / 58;
}

bool obstacleDetected()
{
  return obstacleCounter >= obstacleConfirmCount;
}

// =======================
// LINE FOLLOWING
// =======================

void followLine()
{
  if (s1 == 1 && s2 == 1 && s3 == 1 && s4 == 1 && s5 == 1)
  {
    if (lastDirection == -1)
    {
      actionText = "SEARCH_LEFT";
      searchLeft();
    }
    else if (lastDirection == 1)
    {
      actionText = "SEARCH_RIGHT";
      searchRight();
    }
    else
    {
      if (millis() < resumeSearchUntil)
      {
        actionText = "RESUME_SEARCH_FORWARD";
        searchForward();
      }
      else
      {
        actionText = "STOP_NO_LINE";
        stopMotors();
      }
    }
  }
  else if (s3 == 0 && s2 == 1 && s4 == 1)
  {
    actionText = "FORWARD";
    forward();
  }
  else if (s2 == 0 && s3 == 0)
  {
    lastDirection = -1;
    actionText = "SOFT_LEFT";
    softLeft();
  }
  else if (s3 == 0 && s4 == 0)
  {
    lastDirection = 1;
    actionText = "SOFT_RIGHT";
    softRight();
  }
  else if (s1 == 0 || s2 == 0)
  {
    lastDirection = -1;
    actionText = "HARD_LEFT";
    hardLeft();
  }
  else if (s4 == 0 || s5 == 0)
  {
    lastDirection = 1;
    actionText = "HARD_RIGHT";
    hardRight();
  }
  else
  {
    actionText = "STOP_UNKNOWN";
    stopMotors();
  }
}

// =======================
// SERIT DEGISTIRME
// =======================

void startLaneChange()
{
  obstacleCounter = 0;
  resumeSearchUntil = 0;

  if (currentLane == OUTER_LANE)
  {
    targetLane = INNER_LANE;
    laneChangeDir = INNER_LANE_IS_LEFT ? -1 : 1;
  }
  else
  {
    targetLane = OUTER_LANE;
    laneChangeDir = INNER_LANE_IS_LEFT ? 1 : -1;
  }

  state = LANE_CHANGE_LEAVE;
  stateStartTime = millis();
  actionText = "LANE_CHANGE_START";
}

void doLaneChangeTurn()
{
  if (laneChangeDir == -1)
  {
    actionText = "LANE_CHANGE_LEFT";
    laneChangeLeft();
  }
  else
  {
    actionText = "LANE_CHANGE_RIGHT";
    laneChangeRight();
  }
}

// =======================
// MOTOR HAREKETLERI
// =======================

void forward()
{
  drive(forwardSpeed, forwardSpeed);
}

void softLeft()
{
  drive(softSlowSpeed, softFastSpeed);
}

void softRight()
{
  drive(softFastSpeed, softSlowSpeed);
}

void hardLeft()
{
  drive(hardSlowSpeed, hardFastSpeed);
}

void hardRight()
{
  drive(hardFastSpeed, hardSlowSpeed);
}

void searchLeft()
{
  drive(0, searchSpeed);
}

void searchRight()
{
  drive(searchSpeed, 0);
}

void searchForward()
{
  drive(resumeSearchSpeed, resumeSearchSpeed);
}

void laneChangeLeft()
{
  drive(laneChangeSlowSpeed, laneChangeFastSpeed);
}

void laneChangeRight()
{
  drive(laneChangeFastSpeed, laneChangeSlowSpeed);
}

void drive(int leftSpeed, int rightSpeed)
{
  digitalWrite(STBY, HIGH);

  leftSpeed = constrain(leftSpeed, 0, 255);
  rightSpeed = constrain(rightSpeed, 0, 255);

  digitalWrite(AIN1, HIGH);
  digitalWrite(AIN2, LOW);
  analogWrite(PWMA, leftSpeed);

  digitalWrite(BIN1, HIGH);
  digitalWrite(BIN2, LOW);
  analogWrite(PWMB, rightSpeed);
}

void stopMotors()
{
  analogWrite(PWMA, 0);
  analogWrite(PWMB, 0);

  digitalWrite(AIN1, LOW);
  digitalWrite(AIN2, LOW);
  digitalWrite(BIN1, LOW);
  digitalWrite(BIN2, LOW);

  digitalWrite(STBY, LOW);
}

// =======================
// DEBUG
// =======================

void printDebug()
{
  if (millis() - lastDebugTime < debugInterval)
  {
    return;
  }

  lastDebugTime = millis();

  Serial.print(s1); Serial.print(" ");
  Serial.print(s2); Serial.print(" ");
  Serial.print(s3); Serial.print(" ");
  Serial.print(s4); Serial.print(" ");
  Serial.print(s5);

  Serial.print(" DIST:");
  Serial.print(lastDistanceCm);

  Serial.print(" OBS:");
  Serial.print(obstacleCounter);

  Serial.print(" LANE:");
  if (currentLane == OUTER_LANE) Serial.print("OUTER");
  else Serial.print("INNER");

  Serial.print(" STATE:");
  printStateName();

  Serial.print(" CMD:");
  Serial.print(commandText);

  Serial.print(" STOP:");
  Serial.print(cameraStopActive);

  Serial.print(" ACTION:");
  Serial.println(actionText);
}

void printStateName()
{
  if (state == FOLLOW_LINE) Serial.print("FOLLOW");
  else if (state == LANE_CHANGE_LEAVE) Serial.print("LEAVE");
  else if (state == LANE_CHANGE_FIND) Serial.print("FIND");
  else if (state == LANE_CHANGE_ALIGN) Serial.print("ALIGN");
}
