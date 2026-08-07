#pragma once

#include <QString>

// Хранилище секретов провайдеров (§FR-7, §9).
//
// Ключи живут только в KWallet и никогда — в конфигурации, репозитории,
// фикстурах и диагностике. Ядро их не видит вовсе ([ADR-008](../../../docs/adr/0008-llm-transport-boundary.md)),
// поэтому редактировать в его журнале нечего.
//
// Если кошелёк недоступен, ключ не сохраняется вовсе. Записать его
// «пока что» в обычный файл было бы худшим из решений: пользователь
// считал бы секрет защищённым, не будучи защищённым.
class SecretStore
{
public:
    static bool isAvailable();

    static bool store(const QString &account, const QString &secret);
    static QString read(const QString &account);
    static bool remove(const QString &account);
};
