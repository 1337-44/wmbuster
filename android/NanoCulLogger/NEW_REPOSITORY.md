# Publish this standalone Android project to a NEW GitHub repository

This directory is a complete standalone Android Studio project and does
not depend on the Flipper wM-buster+ source tree.

The GitHub connector in ChatGPT cannot create new repositories. To move
this project to a separate GitHub repository, create an **empty repo**
named `nanocul-gps-logger` under your GitHub account. Leave the GitHub
"Add README", ".gitignore", and "license" options unchecked (these files
are already included).

From Windows PowerShell (inside the project root):

```powershell
git init -b main
git add .
git commit -m "Initial Android NanoCUL USB GPS Logger"
git remote add origin https://github.com/1337-44/nanocul-gps-logger.git
git push -u origin main
git tag v0.1.0
git push origin v0.1.0
```

After the first GitHub Actions workflow completes, download the debug
APK directly from your repo's **Releases > v0.1.0** page.
The tag-triggered workflow automatically uploads
`NanoCUL_GPS_Logger_debug.apk` as a release attachment.
Subsequent versions: commit the changes, push main, create a new tag.
Avoid tagging before the workflow is present on the default branch.

The generated APK is DEBUG signed, intended for testing. Before
production releases, set up a stable signing keystore and protected
credentials. Do not include signing passwords in GitHub source code.
