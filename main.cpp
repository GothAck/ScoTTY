#include <cstdio>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <cstdlib>
#include <deque>
#include <iostream>
#include <map>
#include <string>

#include <QCoreApplication>
#include <QSocketNotifier>
#include <QLocalSocket>
#include <QFile>
#include <QTimer>

#include <docopt/docopt.h>

using namespace std;

namespace scotty {

static const char USAGE[] =
R"(ScoTTY Socket TTY proxy.

    Usage:
      scotty [options] <UNIX_SOCKET>
      scotty (-h | --help)

    Options:
      -a --alt-break           Use alternative ^[ break.
      -b --no-break            Disable ^] break to exit.
      -h --help                Show this screen.
)";

struct termios orig_termios;
QCoreApplication *app;
QLocalSocket *sock;
QSocketNotifier *stdin;
QFile *stdinFile;
QTimer *timer;

int breaks = 0;

void signalHandler(int signum) {
  if (signum == SIGTERM)
    QCoreApplication::quit();
}

void tty_atexit() {
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
  cerr << endl;
}

int main(int argc, char *argv[]) {
  std::map<std::string, docopt::value> args
      = docopt::docopt(USAGE, { argv + 1, argv + argc }, true, "", true);

  app = new QCoreApplication(argc, argv);

  timer = new QTimer(app);

  const auto optSocketName = args["<UNIX_SOCKET>"].asString();
  const auto optNoBreak = args["--no-break"].asBool();
  const uint8_t optBreakChar = args["--alt-break"].asBool() ? 0x1b : 0x1d;

  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);

  if (!(isatty(STDIN_FILENO) && isatty(STDOUT_FILENO))) {
    cerr << "stdio must be a tty" << endl;
    return -1;
  }

  if (tcgetattr(STDIN_FILENO, &orig_termios) < 0) {
    cerr << "Failed to save tty settings" << endl;
    return -2;
  }

  if (atexit(tty_atexit) != 0) {
    cerr << "Failed to register atexit handler" << endl;
    return -3;
  }

  {
    struct termios raw = orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0; raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0) {
      cerr << "Failed to set raw mode" << endl;
      return -2;
    }
  }

  sock = new QLocalSocket(app);
  stdin = new QSocketNotifier(STDIN_FILENO, QSocketNotifier::Read, app);
  stdinFile = new QFile(app);

  sock->connectToServer(QString::fromStdString(optSocketName), QIODevice::ReadWrite | QIODevice::Unbuffered);
  if (!sock->isValid()) {
    cerr << "Unable to open " << optSocketName << endl;
    return -4;
  }
  stdinFile->open(STDIN_FILENO, QIODevice::ReadOnly | QIODevice::Unbuffered);

  timer->setSingleShot(true);
  timer->callOnTimeout([&] {
    while (breaks--) {
        sock->putChar(optBreakChar);
    }
    breaks = 0;
  });

  QCoreApplication::connect(sock, &QLocalSocket::readyRead, [&]() {
    auto count = sock->bytesAvailable();
    QByteArray bytes;
    while (count) {
      bytes = sock->read(count);
      for (int i = 0; i < bytes.length(); i++) {
        cout << bytes[i];
      }
      cout << flush;
      count -= bytes.length();
    }
  });
  QCoreApplication::connect(stdin, &QSocketNotifier::activated, [&]() {
    char c;
    while(stdinFile->getChar(&c)) {
      if (c == optBreakChar && !optNoBreak) {
        if (++breaks >= 3) {
          QCoreApplication::exit();
        }
        if (!timer->isActive()) {
          timer->start(1000);
        }
      } else {
        if (breaks) {
          while (breaks--) {
            sock->putChar(optBreakChar);
          }
          breaks = 0;
        }
        sock->putChar(c);
      }
    }
  });

  QCoreApplication::connect(sock, &QLocalSocket::disconnected, [] {
    QCoreApplication::exit();
  });

  if (!optNoBreak) {
    if (optBreakChar == 0x1d)
      cerr << "Press ^] three times within 1s to disconnect TTY from " << optSocketName << endl;
    else
      cerr << "Press ^[ three times within 1s to disconnect TTY from " << optSocketName << endl;
  }

  return QCoreApplication::exec();
}

} // namespace

int main(int argc, char *argv[]) {
  return scotty::main(argc, argv);
}
