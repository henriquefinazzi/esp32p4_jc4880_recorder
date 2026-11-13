// Servidor web do gravador (rede local): página para listar, ouvir, baixar e apagar gravações.
#pragma once

#include "esp_err.h"

// Inicia o servidor na porta 80, exigindo usuário e senha (HTTP Basic).
// `hostname` é o nome na rede local (http://<hostname>.local).
esp_err_t web_start(const char *user, const char *pass, const char *hostname);

// Troca o usuário e a senha exigidos, com o servidor no ar ou não.
void web_set_credentials(const char *user, const char *pass);

// Troca o nome na rede com o servidor no ar. Sem efeito se estiver parado.
void web_set_hostname(const char *hostname);

// Derruba o servidor e o nome <nome>.local. Conexões em andamento são encerradas.
void web_stop(void);
