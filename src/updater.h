#pragma once
#include <windows.h>
#include <string>

// Versão gravada no build (o GitHub Actions passa -DAPP_VERSION="1.0.<número do build>")
#ifndef APP_VERSION
#define APP_VERSION "1.0.0"
#endif


struct UpdateInfo {
    std::wstring version;   // ex: "1.0.12"
    std::wstring url;       // link do PrecisionFix.exe na release
};

std::wstring AppVersion();
// Consulta a última release no GitHub. true = existe versão mais nova que a atual.
bool CheckForUpdate(UpdateInfo& out);
// Baixa o novo .exe ao lado do atual (PrecisionFix.exe.new). Valida o arquivo.
bool DownloadUpdate(const UpdateInfo& info, std::wstring& newPath);
// Troca o .exe em execução pelo novo e abre a nova versão. Depois disso o app deve fechar.
bool InstallUpdateAndRestart(const std::wstring& newPath);
// Apaga o .exe antigo que sobrou da última atualização.
void CleanupOldVersion();
