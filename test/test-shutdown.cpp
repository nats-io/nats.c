#include "glib/glibp.h"


class App
{
public:
    ~App() {
        // cleanup library
        nats_CloseAndWait(10);
    }
    void run() {
        nats_Open(-1);
        // do work ....
    }
};

App app;

int main() {
    app.run();
    return 0;
}
