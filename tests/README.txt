Test del core portabile (template, JSON, torrent, titolo release):
  g++ -std=c++23 -Isrc tests/core_tests.cpp src/core/*.cpp -o core_tests && ./core_tests
Con Visual Studio: build.bat li esegue automaticamente (target core_tests).


Test v1/v2/hybrid (C++23):
  g++ -std=c++23 -Isrc tests/torrent_tests.cpp src/core/*.cpp -o torrent_tests
  ./torrent_tests
  python tests/torrent_reference.py ./torrent_tests

Su Windows: build.bat esegue tutte le suite registrate in CTest.
Con Python installato CTest verifica anche 162 torrent contro un riferimento
indipendente (hashlib e bencode della libreria standard). Senza Python restano
attive le due suite C++; il controllo indipendente può essere eseguito a parte.

Compatibilità opzionale con libtorrent 2.x (solo dipendenza dei test):
  python -m pip install libtorrent
  python tests/torrent_reference.py ./torrent_tests --libtorrent
Il controllo valida 153 torrent non vuoti e confronta entrambi gli info-hash.
Le 9 fixture senza alcun dato verificano la gestione degli hash vuoti nel core;
la pipeline dell'app e libtorrent rifiutano sorgenti completamente vuote.

Copertura: SHA-256 FIPS (vuoto, abc, un milione di a), formati e default,
file singoli/multipli/vuoti, blocchi e pezzi esatti/parziali, percorsi UTF-8,
padding hybrid, livelli Merkle, radici duplicate, magnet, ripresa e tracker,
metadati corrotti, percorsi duplicati o in conflitto e bencode non canonico.
