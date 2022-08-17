#include <serial_port.h>

#include <termios.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <string.h>
#include <stdio.h>
#include <csignal>
#include <iostream>
#include <thread>

namespace serial2UDP {

serial_port::serial_port(std::string port_name, data_bits_t data_bits, parity_t parity, baudrate_t baudrate)
    : port_name_(port_name), data_bits_(data_bits), parity_(parity), baudrate_(baudrate) {
    /// initialise variables to default values
    init = true;
    memset(&read_buffer_, 0, sizeof(read_buffer_));
    memset(&write_buffer_, 0, sizeof(write_buffer_));

    /// open serial port file descriptor
    serial_port_fd_ = open(port_name_.c_str(), O_RDWR | O_NONBLOCK | O_NOCTTY);
    if (serial_port_fd_ < 0) {
        init = false;
        std::cerr << "[Error] " << errno << " from open: " << strerror(errno) << std::endl;
    }

    /// lock the port
    if (flock(serial_port_fd_, LOCK_EX | LOCK_NB) == -1) {
        init = false;
        std::cerr << "[Error] Failed to lock the file!\n";
    }

    /// start configuring serial port
    struct termios tty_usb;
    if(tcgetattr(serial_port_fd_, &tty_usb) != 0) {
        init = false;
        std::cerr << "[Error] " << errno << " from tcgetattr: " << strerror(errno) << std::endl;
    }

    /// set UART configuration
    cfmakeraw(&tty_usb);
    switch(parity_) {
        case PARITY_NONE: {
            tty_usb.c_cflag &= ~PARENB;
            break;
        }
        case PARITY_EVEN: {
            tty_usb.c_cflag |= PARENB;
            tty_usb.c_cflag &= ~PARODD;
            break;
        }
        case PARITY_ODD: {
            tty_usb.c_cflag |= PARENB;
            tty_usb.c_cflag |= PARODD;
            break;
        }
    }
    tty_usb.c_cflag &= ~PARENB;
    tty_usb.c_cflag &= ~CSTOPB;
    tty_usb.c_cflag |= data_bits_;
    tty_usb.c_cflag &= ~CRTSCTS;
    tty_usb.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty_usb.c_cflag |= CREAD | CLOCAL;

    /// non-canonical mode
    tty_usb.c_lflag &= ~ICANON;
    /// disable echo
    tty_usb.c_lflag &= ~ECHO;
    tty_usb.c_lflag &= ~ECHOE;
    tty_usb.c_lflag &= ~ECHONL;
    tty_usb.c_lflag &= ~ISIG;
    tty_usb.c_lflag &= ~IEXTEN;

    /// disable output processing
    tty_usb.c_oflag &= ~OPOST;

    /// set reading configuration to be blocking with expected bytes to be read
    tty_usb.c_cc[VTIME] = 0;
    tty_usb.c_cc[VMIN] = 0;

    /// set baudrates
    cfsetispeed(&tty_usb, baudrate_);
    cfsetospeed(&tty_usb, baudrate_);

    /// set serial port configuration
    if (tcsetattr(serial_port_fd_, TCSANOW, &tty_usb) != 0) {
        init = false;
        std::cerr << "[Error] " << errno << " from tcsetattr: " << strerror(errno) << std::endl;
    }

    /// register SIGINT
    signal(SIGINT, signal_handler);

    /// set UDP socket
    sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    destination.sin_family = AF_INET;
    destination.sin_port = htons(udp_port);
    destination.sin_addr.s_addr = inet_addr(udp_hostname.c_str());
}

serial_port::serial_port(std::string port_name = "/dev/ttyUSB0")
    : port_name_(port_name) {
    /// initialise variables to default values
    init = true;
    memset(&read_buffer_, 0, sizeof(read_buffer_));
    memset(&write_buffer_, 0, sizeof(write_buffer_));

    /// open serial port file descriptor
    serial_port_fd_ = open(port_name_.c_str(), O_RDWR | O_NONBLOCK | O_NOCTTY);
    if (serial_port_fd_ < 0) {
        init = false;
        std::cerr << "[Error] " << errno << " from open: " << strerror(errno) << std::endl;
    }

    /// lock the port
    if (flock(serial_port_fd_, LOCK_EX | LOCK_NB) == -1) {
        init = false;
        std::cerr << "[Error] Failed to lock the file!\n";
    }

    /// start configuring serial port
    struct termios tty_usb;
    if(tcgetattr(serial_port_fd_, &tty_usb) != 0) {
        init = false;
        std::cerr << "[Error] " << errno << " from tcgetattr: " << strerror(errno) << std::endl;
    }

    /// set UART configuration
    cfmakeraw(&tty_usb);
    switch(parity_) {
        case PARITY_NONE: {
            tty_usb.c_cflag &= ~PARENB;
            break;
        }
        case PARITY_EVEN: {
            tty_usb.c_cflag |= PARENB;
            tty_usb.c_cflag &= ~PARODD;
            break;
        }
        case PARITY_ODD: {
            tty_usb.c_cflag |= PARENB;
            tty_usb.c_cflag |= PARODD;
            break;
        }
    }
    tty_usb.c_cflag &= ~PARENB;
    tty_usb.c_cflag &= ~CSTOPB;
    tty_usb.c_cflag |= data_bits_;
    tty_usb.c_cflag &= ~CRTSCTS;
    tty_usb.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty_usb.c_cflag |= CREAD | CLOCAL;

    /// non-canonical mode
    tty_usb.c_lflag &= ~ICANON;
    /// disable echo
    tty_usb.c_lflag &= ~ECHO;
    tty_usb.c_lflag &= ~ECHOE;
    tty_usb.c_lflag &= ~ECHONL;
    tty_usb.c_lflag &= ~ISIG;
    tty_usb.c_lflag &= ~IEXTEN;

    /// disable output processing
    tty_usb.c_oflag &= ~OPOST;

    /// set reading configuration to be blocking with expected bytes to be read
    tty_usb.c_cc[VTIME] = 0;
    tty_usb.c_cc[VMIN] = 0;

    /// set baudrates
    cfsetispeed(&tty_usb, baudrate_);
    cfsetospeed(&tty_usb, baudrate_);

    /// set serial port configuration
    if (tcsetattr(serial_port_fd_, TCSANOW, &tty_usb) != 0) {
        init = false;
        std::cerr << "[Error] " << errno << " from tcsetattr: " << strerror(errno) << std::endl;
    }

    /// register SIGINT
    signal(SIGINT, signal_handler);

    /// set UDP socket
    sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    destination.sin_family = AF_INET;
    destination.sin_port = htons(udp_port);
    destination.sin_addr.s_addr = inet_addr(udp_hostname.c_str());
}

serial_port::~serial_port() {
    std::cout << "Closing serial_bridge " << port_name_ << std::endl;
    close(serial_port_fd_);
    close(sock);
}

void serial_port::start() {
    std::cout << "Starting serial-bridge!\n" << "------------\n";
    keep_running = true;

    /// run threads
    read_cmd_thread = std::thread(&serial_port::read_commands, this);
    exchange_data_thread = std::thread(&serial_port::exchange_data, this);

    read_cmd_thread.join();
    exchange_data_thread.join();
}

void serial_port::signal_handler(int signum) {
    std::cout << "Interrupt signal (" << signum << ") received.\n";
    exit(signum);
}

void serial_port::read_commands(void) {
    /// keep reading new commands
    while(keep_running) {
        std::cout << "Provide next cmd (e.g. \"XY\"):\n";
        std::cin.getline(curr_cmd, 100);
        /// verify command
        if ((strcmp(curr_cmd, "XA") == 0)
            || (strcmp(curr_cmd, "FY") == 0)) {
            std::cout << "Next command to send: " << std::string(curr_cmd) << std::endl;
            cmd_to_send = true;
        } else {
            std::cerr << "[Error] Wrong command requested\n";
        }
    }
};

void serial_port::exchange_data(void) {
    fd_set rdset, wrset;
    int r, n;
    int sig_read;
    /// define json telemetry
    static json telemetry = {
        {"uid", 0},
        {"cookie", 0},
        {"cpu_temp", 0.0},
        {"switch_on", false},
    };

    while (keep_running) {
        struct timeval tv, *ptv;

        ptv = NULL;
        FD_ZERO(&rdset);
        FD_ZERO(&wrset);

        FD_SET(serial_port_fd_, &rdset);

        if(cmd_to_send) {
            FD_SET(serial_port_fd_, &wrset);
        }

        r = select(serial_port_fd_ + 1, &rdset, &wrset, NULL, ptv);
        if ( r < 0 )  {
            if ( errno == EINTR )
                continue;
            else {
                std::cerr << "[Error] select failed " << errno << " " << strerror(errno) << std::endl;
            }

        } else if ( r == 0 ) {
            std::cerr << "[Error] timeout!\n";
        }


        /// reception
        if ( FD_ISSET(serial_port_fd_, &rdset) ) {
            /// read from port byte by byte
            volatile int i =0;
            do {
                n = read(serial_port_fd_, &read_buffer_[i], 1);
                i++;
            } while (n != 0 && errno != EINTR);
            if ( n < 0 ) {
                if ( errno != EAGAIN && errno != EWOULDBLOCK )
                    std::cerr << "[Error] read from port failed: " << strerror(errno) << std::endl;
            }

            if (static_cast<int>(read_buffer_[0]) != uid) {
                std::cerr << "[Error] wrong init character\n";
            } else {
                /// update telemetry in JSON format
                update_telemetry_in_json(telemetry);

                std::string telemetry_string = telemetry.dump();
                std::cout << telemetry_string << std::endl;

                /// send JSON over UDP
                int n_bytes = ::sendto(sock, telemetry_string.c_str(), telemetry_string.length(), 0, reinterpret_cast<sockaddr*>(&destination), sizeof(destination));
                std::cout << n_bytes << " bytes sent" << std::endl;
            }
        }

        /// transmission
        if ( FD_ISSET(serial_port_fd_, &wrset) ) {
            /// write command over port
            n = write(serial_port_fd_, curr_cmd, WRITE_SIZE);
            if ( n <= 0 ) {
                std::cerr << "[Error] write to port failed: " << strerror(errno) << std::endl;
            }
            cmd_to_send = false;
        }
    }
}

void serial_port::update_telemetry_in_json(json& telemetry) {
    if (data_bits_ == BITS_7) {
        telemetry["uid"] = static_cast<int>(read_buffer_[0] & 0x7F);
        telemetry["cookie"] = static_cast<int>((read_buffer_[4] & 0x7F) | ((read_buffer_[5] & 0x7F) << 8)
                | ((read_buffer_[6] & 0x7F) << 16) | ((read_buffer_[7] & 0x7F) << 24));
        telemetry["cpu_temp"] = static_cast<int>((read_buffer_[8] & 0x7F) | (read_buffer_[9] & 0x7F) << 8);
        telemetry["switch_on"] = static_cast<int>(read_buffer_[10] & 0x7F);
    } else if (data_bits_ == BITS_8) {
        telemetry["uid"] = static_cast<int>(read_buffer_[0]);
        telemetry["cookie"] = static_cast<int>((read_buffer_[4]) | (read_buffer_[5] << 8)
                | (read_buffer_[6] << 16) | (read_buffer_[7] << 24));
        telemetry["cpu_temp"] = static_cast<int>((read_buffer_[8]) | (read_buffer_[9] << 8));
        telemetry["switch_on"] = static_cast<int>(read_buffer_[10]);
    } else {
        std::cerr << "[Error] " << data_bits_ << " data bits is not supported!\n";
    }

}

}