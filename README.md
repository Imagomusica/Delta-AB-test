# Dune

Égaliseur de mastering à filtres FIR, 8 bandes. Interface noir et blanc, trois thèmes : White 9016, Anthracite 7016, Black 9005.

## Fonctions (v0.2)
- **3 modes de phase** : Linéaire (latence déclarée au DAW), Naturelle (phase des filtres analogiques équivalents, ramenée à la phase minimale près de Nyquist), Minimale pure.
- **Résolution** : filtres de 0,35 s à 2,8 s (Standard 1,4 s). Plus c'est long, plus les graves sont précis.
- **8 bandes** : cloche, shelf grave, shelf aigu, coupe-bas, coupe-haut (6 à 96 dB/oct), notch. Chaque bande agit en ST, M ou S.
- **Noms de notes** avec diapason réglable (La 415 à 444).
- **Tilt** : bascule douce autour de 650 Hz, ±6 dB, bouton ON/OFF.
- **Modulomètre** : VU, PPM (quasi-crête), RMS, true peak 4x. Clic sur le cadre pour remettre la crête à zéro.
- **Champ de sable** : les dunes suivent la courbe de l'EQ et le spectre. La ligne brillante est la courbe exacte (±18 dB).
- Sans cramping près de Nyquist, 64 bits, bypass compensé en latence, gain de sortie.

## Installation
Le build macOS est fait par GitHub Actions (onglet Actions). Commande d'installation dans le chat.

## Compiler soi-même
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j8
```
Le plugin est installé automatiquement dans les dossiers VST3/AU de l'utilisateur.

## Tests du cœur DSP
```bash
g++ -O2 -std=c++17 Tests/design_test.cpp -o design_test
mkdir out && ./design_test out 96000
python3 Tests/validate.py out 96000
```

## Pas encore fait
Compresseur split (pré/post EQ), commutateur « Nagra ». Ces deux points sont à définir.
