#include <ros.h>
#include <geometry_msgs/Twist.h>
#include <sensor_msgs/JointState.h>
#include <DynamixelSoftSerial.h>
#include <SoftwareSerial.h>

#define RATE_MS                   20    // задержка для публикации

#define WHEEL_IMPULSE_COUNT       172   // Количество импульсов на оборот колеса
#define GRAD_RUDDER               65.0  // угол поворота колес между положениями "влево" - "вправо" (в градусах)
#define RUDDER_DATA_CONTROL_PIN   4     // pin control for data transmission and recepcionde
#define RUDDER_RX_PIN             7     // softSerial RX pin для подключения Dynamixel AX-12a
#define RUDDER_TX_PIN             8     // softSerial TX pin для подключения Dynamixel AX-12a
#define DYNAMIXEL_ID              3     // dynamixel AX-12a

//http://savageelectronics.blogspot.com/2011/08/actualizacion-biblioteca-dynamixel.html
//http://hobbytech.com.ua/dynamixel-ax-12a-%D0%B8-arduino-%D0%BA%D0%B0%D0%BA-%D0%B8%D1%81%D0%BF%D0%BE%D0%BB%D1%8C%D0%B7%D0%BE%D0%B2%D0%B0%D1%82%D1%8C-%D0%BF%D0%BE%D1%81%D0%BB%D0%B5%D0%B4%D0%BE%D0%B2%D0%B0%D1%82%D0%B5%D0%BB/

#define ENCODER_LEFT              2     // прерывание на пине 2
#define ENCODER_RIGHT             3     // прерывание на пине 3
#define MOTOR_PIN_1               5     // двигатель +/-
#define MOTOR_PIN_2               6     // двигатель -/+
#define DIRECTION_MOTOR_LEFT      9     // выход с левого мотора (для направления движения)
#define DIRECTION_MOTOR_RIGHT     10    // выход с правого мотора (для направления движения) 

#define NUM_JOINTS                3

char *state_names[NUM_JOINTS] = {"left_wheel", "right_wheel", "rudder"};
float state_pos[NUM_JOINTS] = {0, 0, 0};
float state_vel[NUM_JOINTS] = {0, 0, 0};  //rad/s
float state_eff[NUM_JOINTS] = {0, 0, 0};
unsigned long last_ms;

ros::NodeHandle nh;

float linear;
float angular;

void drive_cb(const geometry_msgs::Twist& cmd_vel) {
  linear = map(cmd_vel.linear.x * 1000, -1000, 1000, -255, 255);
  angular = cmd_vel.angular.z;
  Dynamixel.move(DYNAMIXEL_ID, angular2dynamixel(angular));
  if (linear > 1) {
    analogWrite(MOTOR_PIN_1, linear);
    analogWrite(MOTOR_PIN_2, 0);
  }
  else if (linear < -1) {
    analogWrite(MOTOR_PIN_2, -linear);
    analogWrite(MOTOR_PIN_1, 0);
  }
  else if (linear == 0) {
    analogWrite(MOTOR_PIN_2, 0);
    analogWrite(MOTOR_PIN_1, 0);
  }
}

sensor_msgs::JointState state_msg;

ros::Publisher state_pub("joint_states", &state_msg);
ros::Subscriber<geometry_msgs::Twist> drive_sub("cmd_vel", drive_cb);

void setup() {
  pinMode(MOTOR_PIN_1, OUTPUT);
  pinMode(MOTOR_PIN_2, OUTPUT);
  pinMode(ENCODER_LEFT, INPUT);
  pinMode(ENCODER_RIGHT, INPUT);
  pinMode(DIRECTION_MOTOR_LEFT, INPUT);
  pinMode(DIRECTION_MOTOR_RIGHT, INPUT);
  attachInterrupt(0, doEncoderLeft, CHANGE);  //pin2
  attachInterrupt(1, doEncoderRight, CHANGE); //pin3
  
  Dynamixel.begin(115200, RUDDER_RX_PIN, RUDDER_TX_PIN, RUDDER_DATA_CONTROL_PIN);
  Dynamixel.move(DYNAMIXEL_ID, 512);
  
  nh.getHardware()->setBaud(500000);
  nh.initNode();
  nh.subscribe(drive_sub);
  nh.advertise(state_pub);

  state_msg.header.frame_id =  "/driver_states";
  state_msg.name_length = NUM_JOINTS;
  state_msg.velocity_length = NUM_JOINTS;
  state_msg.position_length = NUM_JOINTS;
  state_msg.effort_length = NUM_JOINTS;
  state_msg.name = state_names;
  state_msg.position = state_pos;
  state_msg.velocity = state_vel;
  state_msg.effort = state_eff;
}

void loop() {
    unsigned long t = millis() - last_ms;
  if ((t) >= RATE_MS) {
    last_ms = millis();
    state_pos[0] = impulse2radian(state_pos[0]);     //rad
    state_pos[1] = impulse2radian(state_pos[1]);     
    state_pos[2] = angular;                          //rad
    state_vel[0] = state_pos[0]/(t/1000.0);          //rad/s
    state_vel[1] = state_pos[1]/(t/1000.0);   
    state_msg.header.stamp = nh.now();
    state_pub.publish(&state_msg);
    state_pos[0] = 0;
    state_pos[1] = 0;
    state_pos[2] = 0;
  }
  nh.spinOnce();
}

float impulse2radian(int x){
  return (x / (WHEEL_IMPULSE_COUNT / 360)) * M_PI / 180;  //преобразование импульсы > градусы > радианы
}

float angular2dynamixel(float angular){
  float dynamixel_value = 0;
  float angular_grad = angular*180/M_PI;                     //преобразование радиан > градусы
  float angular_value_res = ((1023/360)*(GRAD_RUDDER/2));    //значение для динамикселя в одном направлении
  
  if (angular_grad >= -(GRAD_RUDDER/2) & angular_grad <= (GRAD_RUDDER/2))   //Если в пределах возможного - ремапим значения для динамикселя
  {
    dynamixel_value = map(angular_grad*10, -(GRAD_RUDDER/2)*10, (GRAD_RUDDER/2)*10, 512-angular_value_res, 512+angular_value_res); //ремапим значения угла поворота на значения динамикселя (с ограничением угла поворота до возможного выворота колес)
  }
  else 
  {
    if (angular_grad < -(GRAD_RUDDER/2))
    {
      dynamixel_value = 512-angular_value_res;   //программное ограничение поворота колес на максимальный градус влево
    }
    else
    {
      dynamixel_value = 512+angular_value_res;   //программное ограничение поворота колес на максимальный градус вправо
    }
  }
  return dynamixel_value;
}

// Interrupt
void doEncoderLeft() {
  state_pos[0] += getEncoderCount(DIRECTION_MOTOR_LEFT, 0);
}
void doEncoderRight() {
  state_pos[1] += getEncoderCount(DIRECTION_MOTOR_RIGHT, 1);
}

int getEncoderCount(int direction_motor, int side)
{
  int count = 0;
  // Левое колесо
  if (side == 0){
    if (digitalRead(direction_motor) == HIGH)
    {
      count++;
    }
    else
    {
      count--;
    }
  }
  // Правое колесо
  if (side == 1){
    if (digitalRead(direction_motor) == LOW)
    {
      count++;
    }
    else
    {
      count--;
    }
  }
  return count;
}
