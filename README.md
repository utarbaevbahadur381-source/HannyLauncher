# Hanny Launcher (C++ / Qt 6)

Лаунчер Minecraft Java Edition: список версий Mojang, загрузка клиента, библиотек и ассетов, офлайн-ник, выбор ОЗУ и Java.

## Самый простой способ получить .exe (через GitHub)

1. Создай новый репозиторий на GitHub (можно приватный).
2. Залей **всю** папку `HannyLauncher` в репозиторий (включая папку `.github`).
3. Зайди во вкладку **Actions** → выбери workflow **Build Windows EXE** → **Run workflow**.
4. Подожди 3–6 минут.
5. Внизу страницы workflow скачай артефакт **HannyLauncher-Windows.zip**.
6. Распакуй — внутри будет `HannyLauncher.exe` и все нужные DLL.

Готово. Можно сразу запускать.

---

## Сборка вручную

**Windows:**
```
cmake -B build -DCMAKE_PREFIX_PATH=C:/Qt/6.x.x/msvc2022_64
cmake --build build --config Release
windeployqt build/Release/HannyLauncher.exe
```

**Linux:**
```
sudo apt install qt6-base-dev cmake g++
cmake -B build && cmake --build build -j
./build/HannyLauncher
```

## Нужна Java
- 1.21+ → Java 21  
- 1.18–1.20 → Java 17  
- старше → Java 8/16  

(лаунчер показывает нужную версию для установленной сборки)

## Ограничения прототипа
- Только офлайн-режим (без входа через Microsoft).
- Надёжно работают версии 1.13+; у старых версий natives-библиотеки не распаковываются.
- Forge/Fabric и свои сборки пока не поддерживаются.
