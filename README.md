# Dune v0.1

Égaliseur de mastering à filtres FIR, 8 bandes, avec phase naturelle (minimale) ou linéaire.

## Ce qu'il fait
- **8 bandes** : cloche, shelf bas/haut, passe-haut/passe-bas (6 à 96 dB/oct, Butterworth), notch.
- **Chaque bande** agit sur Stéréo, Gauche, Droite, **Mid** ou **Side** (matrice M/S exacte, sans étage supplémentaire).
- **Sans cramping** : les courbes sont évaluées directement sur la grille de fréquences à partir de prototypes analogiques. Une cloche à 18 kHz garde sa forme jusqu'à Nyquist.
- **Phase naturelle** (minimale, calculée par cepstre) : latence nulle, la phase d'un égaliseur analogique.
- **Phase linéaire** : latence L/2 déclarée à l'hôte, sans déphasage entre les fréquences.
- **Résolution** (Standard à Maximum) : longueur du filtre, donc précision dans les graves.
- Convolution partitionnée de JUCE (faible charge CPU), filtre recalculé en tâche de fond, fondu enchaîné quand tu bouges un réglage.
- Interface : courbe de réponse, nœuds déplaçables, analyseur de spectre, gain de sortie, bypass compensé en latence.

## Interface
- Glisse un nœud : fréquence et gain.
- Molette sur un nœud : Q (ou pente pour un passe-haut/bas).
- Double-clic dans le vide : ajoute une bande. Double-clic sur un nœud : la désactive.
- En bas : choisis la bande (1 à 8), puis type, canal (Stéréo/Gauche/Droite/Mid/Side) et réglages.

## Installation (macOS, Terminal)
Prérequis (une seule fois) : `xcode-select --install` et `brew install cmake`.

```bash
cd ~/Downloads
unzip -o Dune_v0.1.zip
cd Dune
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j8
```
Le premier `cmake` télécharge JUCE (environ 1 minute). La compilation installe automatiquement :
- `~/Library/Audio/Plug-Ins/VST3/Dune.vst3`
- `~/Library/Audio/Plug-Ins/Components/Dune.component` (AU)

Relance ensuite le scan des plugins dans ton DAW. L'application autonome est dans `build/Dune_artefacts/Release/Standalone/`.

Pour un binaire universel (Intel + Apple Silicon) : ajoute `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` à la première commande cmake.

## Windows (PowerShell, Visual Studio 2022 + CMake installés)
```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```
Lance PowerShell en administrateur pour que la copie vers `C:\Program Files\Common Files\VST3` réussisse.

## Linux
```bash
sudo apt install build-essential cmake git libasound2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libgl1-mesa-dev
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j8
```
Le VST3 est copié dans `~/.vst3`.

## Tests du cœur DSP
```bash
g++ -O2 -std=c++17 Tests/design_test.cpp -o design_test
mkdir out && ./design_test out 96000
python3 Tests/validate.py out 96000
```
`validate.py` compare chaque filtre produit à une implémentation indépendante en numpy.

## État de la v0.1
- Cœur DSP validé : erreur de réponse de 0,00 dB dans la plupart des cas, 0,06 dB au pire (cloche ou passe-haut à 30 Hz, résolution Standard).
- Plugin compilé sous Linux en test. **Pas encore essayé dans un DAW** : les retours de ton écoute et de tes mesures décideront de la suite.
- Pas encore : fenêtrage dépendant de la fréquence, phase mixte, mode analogique non linéaire, mesures de comparaison avec MAAT/Weiss/FabFilter/Acourate.

## Licence
Utilise JUCE (AGPLv3 ou licence commerciale) et le SDK VST3. Pour un usage personnel c'est sans souci ; pour distribuer ou vendre le plugin, prends une licence JUCE adaptée.
