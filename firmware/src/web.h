// Recorder web server (local network): page to list, play, download and delete recordings.
#pragma once

#include "esp_err.h"

// Starts the server on port 80, requiring a username and password (HTTP Basic).
// `hostname` is the name on the local network (http://<hostname>.local).
esp_err_t web_start(const char *user, const char *pass, const char *hostname);

// Changes the required username and password, whether the server is running or not.
void web_set_credentials(const char *user, const char *pass);

// Changes the network name while the server is running. No effect if it is stopped.
void web_set_hostname(const char *hostname);

// Shuts down the server and the <name>.local name. Open connections are closed.
void web_stop(void);
