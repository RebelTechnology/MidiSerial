#include "../JuceLibraryCode/JuceHeader.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <strings.h>
#include <unistd.h>
#include <iostream>

#define DEFAULT_SPEED B38400
#define DEFAULT_PORT "/dev/ttyS1"

class MidiSerial : public juce::MidiInputCallback {
private:
  int fd;
  bool verbose;
  MidiOutput* midiout;
  MidiInput* midiin;

  juce::String print(const MidiMessage& msg){
    juce::String str;
    for(int i=0; i<msg.getRawDataSize(); ++i){
      str += " 0x";
      str += juce::String::toHexString(msg.getRawData()[i]);
    }
    return str;
  }

  void listDevices(const StringArray& names){
    for(int i=0; i<names.size(); ++i)
      std::cout << i << ": " << names[i] << std::endl;
  }

public:
  void handleIncomingMidiMessage(MidiInput *source,
                                 const MidiMessage &msg){
    if(write(fd, msg.getRawData(), msg.getRawDataSize()) != msg.getRawDataSize())
      perror("write failed");
    if(verbose)
      std::cout << "tx" << print(msg) << std::endl;
  }

  void usage(){
    std::cerr << "MidiSerial v1"  << std::endl << "usage:" << std::endl
              << "-p FILE\t set serial port" << std::endl
              << "-s NUM\t set serial speed (default: 38400)" << std::endl
              << "-v\t verbose, prints messages sent/received" << std::endl
              << "-i NUM\t set MIDI input device" << std::endl
              << "-o NUM\t set MIDI output device" << std::endl
              << "-c NAME\t create MIDI input/output device" << std::endl
              << "-l\t list MIDI input/output devices and exit" << std::endl
              << "-h or --help\tprint this usage information and exit" << std::endl;
  }

  int run(int argc, char* argv[]) {
    midiin = NULL;
    midiout = NULL;
    juce::String port = T(DEFAULT_PORT);
    int speed = DEFAULT_SPEED;
    for(int i=1; i<argc; ++i){
      juce::String arg = juce::String(argv[i]);
      if(arg.compare("-p") == 0 && ++i < argc){
        port = juce::String(argv[i]);
      }else if(arg.compare("-v") == 0){
        verbose = true;
      }else if(arg.compare("-l") == 0){
        std::cout << "MIDI output devices:" << std::endl;
        listDevices(MidiOutput::getDevices());
        std::cout << "MIDI input devices:" << std::endl;
        listDevices(MidiInput::getDevices());
        return 0;
      }else if(arg.compare("-s") == 0 && ++i < argc){
        speed = juce::String(argv[i]).getIntValue();
      }else if(arg.compare("-o") == 0 && ++i < argc && midiout == NULL){
        int index = juce::String(argv[i]).getIntValue();
        midiout = MidiOutput::openDevice(index);
        if(verbose)
          std::cout << "Opening MIDI output: " << MidiOutput::getDevices()[index] << std::endl;
      }else if(arg.compare("-i") == 0 && ++i < argc && midiin == NULL){
        int index = juce::String(argv[i]).getIntValue();
        midiin = MidiInput::openDevice(index, this);
        if(verbose)
          std::cout << "Opening MIDI input: " << MidiInput::getDevices()[index] << std::endl;
      }else if(arg.compare("-c") == 0 && ++i < argc && midiin == NULL && midiout == NULL){
        String name = juce::String(argv[i]);
        midiout = MidiOutput::createNewDevice(name);
        midiin = MidiInput::createNewDevice(name, this);
      }else if(arg.compare("-h") == 0 || arg.compare("--help") == 0 ){
        usage();
        return 0;
      }else{
        usage();
        errno = EINVAL;
        perror(arg.toUTF8());
        return -1;
      }
    }

    if(midiin == NULL && midiout == NULL){
      // default behaviour if no interface specified
      midiout = MidiOutput::createNewDevice(T("MidiSerial"));
      midiin = MidiInput::createNewDevice(T("MidiSerial"), this);
    }

    ssize_t len;
    unsigned char buf[255];
    struct termios tio, oldtio;

    int oflag = O_RDWR | O_NOCTTY | O_NONBLOCK;

    fd = open(port.toUTF8(), oflag);
    if(fd <0){
      perror(port.toUTF8()); 
      return -1; 
    }

    tcgetattr(fd, &oldtio); /* save current port settings */

    cfmakeraw(&tio);
    if(cfsetispeed(&tio, speed) | cfsetospeed(&tio, speed)) // non-lazy logic
      perror(juce::String(speed).toUTF8());
    if(tcsetattr(fd, TCSANOW, &tio)){
      perror(port.toUTF8());
      //       return -1;
    }

    if(verbose)
      std::cout << "tty " << port << " at " << cfgetispeed(&tio) << " baud" << std::endl;

    //     fcntl(fd, F_SETFL, FNDELAY); // set non-blocking read
    fcntl(fd, F_SETFL, 0); // set blocking read

    if(midiin != NULL)
      midiin->start();

    juce::MidiMessage msg;
    int used = 0;
    for(;;) {
      bzero(&buf[0], sizeof(buf));
      len = read(fd, &buf[0], 255);
      if(len > 0) {
        msg = juce::MidiMessage(buf, len, used, msg.getRawData()[0]);
        if(midiout != NULL)
          midiout->sendMessageNow(msg);
        if(verbose)
          std::cout << "rx" << print(msg) << std::endl;
      }
    }
        
    tcsetattr(fd, TCSANOW, &oldtio);
    close(fd);

    return 0;
  }

  ~MidiSerial(){
    if(midiin != NULL){
      midiin->stop();
      delete midiin;
    }
    if(midiout != NULL)
      delete midiout;
  }
};

int main (int argc, char* argv[]) {
  const ScopedJuceInitialiser_NonGUI juceSystemInitialiser;
  MidiSerial service;
  int val = service.run(argc, argv);
  return val;
}
