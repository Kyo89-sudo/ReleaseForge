# Pubblicare una versione

Repository: https://github.com/Kyo89-sudo/ReleaseForge

1. Aggiornare versione in CMakeLists.txt, src/resource.h,
   src/app/ReleaseForge.manifest e installer/ReleaseForge.iss.
2. Completare il changelog con le modifiche e la data della release.
3. Eseguire build.bat e verificare manualmente le modifiche all'interfaccia.
4. Creare commit e tag vVERSIONE sul repository esistente.
5. Pubblicare la release con archivio portabile, sorgenti e checksum SHA-256.

Il portabile deve includere il nuovo eseguibile, README.md, LICENSE,
THIRD-PARTY-NOTICES.txt e CHANGELOG.txt. Non riutilizzare eseguibili di versioni
precedenti. Conservare i crediti e la licenza delle parti derivate da makerelease.
