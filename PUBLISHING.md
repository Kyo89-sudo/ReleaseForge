# Pubblicare ReleaseForge

La versione 0.1.0 avvia la numerazione pubblica; la cronologia delle build locali
non viene presentata come una serie di release pubbliche.

Il codice aggiunto è pubblicato sotto licenza MIT, copyright 2026 Kyos.
THIRD-PARTY-NOTICES.txt conserva i crediti e la licenza delle parti di makerelease.

1. Verificare che il repository contenga LICENSE e THIRD-PARTY-NOTICES.txt.
2. Eseguire build.bat su Windows e verificare manualmente generazione, impostazioni e upload.
3. Creare un repository GitHub vuoto, senza README o licenza generati da GitHub.
4. Dalla cartella del progetto eseguire:

```powershell
git init -b main
git add .
git diff --cached --stat
git commit -m "Initial public release 0.1.0"
git remote add origin https://github.com/UTENTE/ReleaseForge.git
git push -u origin main
git tag -a v0.1.0 -m "ReleaseForge 0.1.0"
git push origin v0.1.0
```

Su GitHub creare la Release dal tag v0.1.0. Allegare il nuovo eseguibile,
README.md, LICENSE e THIRD-PARTY-NOTICES.txt in un archivio portabile, oppure
l'installer ricompilato. Il vecchio eseguibile 4.0.2 non è incluso in questi
sorgenti e non deve essere presentato come build della 0.1.0.

Quando si cambia versione aggiornare CMakeLists.txt, src/resource.h,
src/app/ReleaseForge.manifest e installer/ReleaseForge.iss. L'interfaccia,
le risorse Windows, il campo createdBy dei torrent e lo user agent HTTP usano
le macro di src/resource.h.
