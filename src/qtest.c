#include <stdio.h>

#define SERIAL_RECEIVING_BUFFER_SIZE 5

char serial_receiving_buffer[SERIAL_RECEIVING_BUFFER_SIZE];

#define SERIAL_CIRCULAR_INC(a) ( (a==SERIAL_RECEIVING_BUFFER_SIZE-1) ? 0 : a+1)

// point to next byte to read
int serial_receiving_reading_index=0;

// point to next byte to write
int serial_receiving_writing_index=0;

int serial_receiving_buffer_write(char *buffer, int len)
{
  int wrote=0;
  int i=0;

  while(SERIAL_CIRCULAR_INC(serial_receiving_writing_index) != serial_receiving_reading_index && i<len){
    serial_receiving_buffer[serial_receiving_writing_index] = buffer[i];
    serial_receiving_writing_index = SERIAL_CIRCULAR_INC(serial_receiving_writing_index);
    i++;
    wrote++;
  }

  return wrote;
}

int serial_receiving_buffer_read(char *buffer, int len)
{
  int i=0;
  int read=0;

  while(serial_receiving_writing_index != serial_receiving_reading_index && i<len){
    buffer[i] = serial_receiving_buffer[serial_receiving_reading_index];
    serial_receiving_reading_index = SERIAL_CIRCULAR_INC(serial_receiving_reading_index);
    i++;
    read++;
  }
  return read;
}


int main(int argc, char *argv[]){

  char buf;

  while(serial_receiving_buffer_read(&buf,1)){
    printf("%c\n",buf);
  }

  printf("adding(1) = %d\n",serial_receiving_buffer_write("1",1));
  printf("adding(2) = %d\n",serial_receiving_buffer_write("2",1));
  printf("adding(3) = %d\n",serial_receiving_buffer_write("3",1));
  printf("adding(4) = %d\n",serial_receiving_buffer_write("4",1));
  printf("adding(5) = %d\n",serial_receiving_buffer_write("5",1));
  printf("adding(6) = %d\n",serial_receiving_buffer_write("6",1));

  while(serial_receiving_buffer_read(&buf,1)){
    printf("%c\n",buf);
  }

  printf("adding(1) = %d\n",serial_receiving_buffer_write("1",1));
  printf("adding(2) = %d\n",serial_receiving_buffer_write("2",1));
  printf("adding(3) = %d\n",serial_receiving_buffer_write("3",1));
  printf("adding(4) = %d\n",serial_receiving_buffer_write("4",1));
  printf("adding(5) = %d\n",serial_receiving_buffer_write("5",1));
  printf("adding(6) = %d\n",serial_receiving_buffer_write("6",1));

  while(serial_receiving_buffer_read(&buf,1)){
    printf("%c\n",buf);
  }


  printf("adding(1) = %d\n",serial_receiving_buffer_write("1",1));
  printf("adding(2) = %d\n",serial_receiving_buffer_write("2",1));
  printf("adding(3) = %d\n",serial_receiving_buffer_write("3",1));

  while(serial_receiving_buffer_read(&buf,1)){
    printf("%c\n",buf);
  }

  printf("adding(1) = %d\n",serial_receiving_buffer_write("1",1));
  printf("adding(2) = %d\n",serial_receiving_buffer_write("2",1));
  printf("adding(3) = %d\n",serial_receiving_buffer_write("3",1));

  while(serial_receiving_buffer_read(&buf,1)){
    printf("%c\n",buf);
  }

  printf("adding(1) = %d\n",serial_receiving_buffer_write("1",1));
  printf("adding(2) = %d\n",serial_receiving_buffer_write("2",1));
  printf("adding(3) = %d\n",serial_receiving_buffer_write("3",1));

  while(serial_receiving_buffer_read(&buf,1)){
    printf("%c\n",buf);
  }

}
