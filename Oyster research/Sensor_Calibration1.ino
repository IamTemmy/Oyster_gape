//This version works, enter m to make paltform back and forth
//while you determine the step size (steps_num). The data is smoothed with
//a 10 point moving average (array_length).
//To collect data, choose s, after placing the platform at a min or max position.
//Choose the step size and number of steps, the a 0 to move towards the sensor or 1 to move away
//Cut and paste data from monitor to create a CSV
//Kamal Ali, 02/09/2024


const int array_length = 10;
char mode = NULL;
int steps_num = NULL;
long count = 0;
int sensor_voltage[array_length];
int sum = 0;
bool dir;
int N; //Number of readings

void step(int steps_num, bool dir);
void welcome_text();
void handle_input();
void print_average();
void update_count();

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  pinMode(18, OUTPUT); //step
  pinMode(19, OUTPUT);  // direction
  welcome_text();
}

void loop() {
  if(Serial.available()) {
    handle_input();    
  }
}

void handle_input() {
  switch(mode) {
    case NULL:
      switch(Serial.read()) {
        case 's':
          mode = 's';
          Serial.println("You entered setup mode, Enter the values: S,N,D");      
          Serial.println("S = steps per reading");
          Serial.println("N = number of readings");
          Serial.println("D = direction, 0 for f and 1 for b");
          break;
        case 'm':
          mode = 'm';
          Serial.println("Enter number of steps:");
          break;
        default: 
          Serial.println("bad command");
          break;
      }
      break;
    case 's':
        while(!Serial.available()){
          // wait for input
        }
          steps_num = Serial.parseInt();
          N = Serial.parseInt();
          dir = Serial.parseInt();
          for(int i =0; i < N; i++){
            step(steps_num, dir);
            print_average();
          }
      break;
    case 'm':
      if(steps_num) {
        switch(Serial.read()) {
          case 'f':
            step(steps_num, 0);
            print_average();
            break;
          case 'b':
            step(steps_num, 1);
            print_average();
            break;
          case 'q':
            mode = NULL;
            steps_num = NULL;
            welcome_text();
            break;
          default:
            Serial.println("You entered an invalid command.");
            break;
        }
      } else {
        steps_num = Serial.parseInt();
        Serial.println("enter f for forward or b for back.... or q to restart:");
      }
      break;
  }
}

void welcome_text(){
  Serial.println("enter s for setup or m for Manual");
}

void step(int steps_num, bool dir){ //steps the motor 
  digitalWrite(19, dir);   //DIRECTION
  for(int i = 0; i < steps_num; i++){
    digitalWrite(18, HIGH); //STEP  
    delayMicroseconds(50);  // NO LOWER THAN 50 Microseconds FOR IT TO WORK 
    digitalWrite(18, LOW); //step
    delayMicroseconds(50);
    if(dir){
      count--;
      }
      else{
      count++;
      }

    sensor_voltage[abs(count) % array_length] = analogRead(A0);
      sum = 0;
      for(int i=0; i < array_length; i++){
      sum += sensor_voltage[i];
    }
  }
} 

void print_average() {
  Serial.print(count);
  Serial.print(", ");    
  Serial.println(sum/array_length);
} 

