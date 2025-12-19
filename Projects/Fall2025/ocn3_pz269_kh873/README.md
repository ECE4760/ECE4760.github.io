# Pico W Audio Streaming Project Website

This is a portable website that can be viewed by opening `index.html` in any web browser.

## Viewing the Website

### Option 1: Using a Local Web Server (Recommended)

For the best experience and to ensure all assets load correctly:

**Using Python (Built into macOS/Linux):**
```bash
python3 -m http.server 8000
```

**Using Python (Windows):**
```bash
python -m http.server 8000
```

Then open your browser to: `http://localhost:8000`

**Using Node.js:**
```bash
npx serve
```

**Using PHP:**
```bash
php -S localhost:8000
```

### Option 2: Direct File Opening

You can double-click `index.html` to open it directly in your browser. Most assets will work, but some browsers may block the video from playing due to CORS restrictions.

#### Windows 11/10 Detailed Instructions:

**Method A: Simple Double-Click (Easiest)**
1. Navigate to the `final_project_website` folder in File Explorer
2. Double-click `index.html`
3. The file will open in your default browser (usually Edge or Chrome)
4. Most features will work, but video playback may be restricted

**Method B: Open With Specific Browser**
1. Right-click `index.html`
2. Select "Open with" → Choose your preferred browser
   - Microsoft Edge (recommended for Windows 11)
   - Google Chrome
   - Firefox
   - Brave

**Method C: Chrome with File Access (If video won't play)**
1. Close all Chrome windows completely
2. Press `Win + R` to open Run dialog
3. Type one of these commands:

   **If Chrome is in default location:**
   ```
   "C:\Program Files\Google\Chrome\Application\chrome.exe" --allow-file-access-from-files --disable-web-security --user-data-dir="C:\temp\chrome_dev"
   ```

   **Or use the shortcut method:**
   - Right-click the Chrome icon on your desktop
   - Select "Properties"
   - In the "Target" field, add this to the end:
     ```
     --allow-file-access-from-files --disable-web-security --user-data-dir="C:\temp\chrome_dev"
     ```
   - Click "OK"
   - Launch Chrome from that shortcut

4. Navigate to the folder and open `index.html`
5. **Warning:** Only use this for local development. Don't browse the internet with these flags enabled.

**Method D: Edge with File Access**
1. Press `Win + R`
2. Type:
   ```
   msedge.exe --allow-file-access-from-files "C:\path\to\your\index.html"
   ```
   Replace `C:\path\to\your\` with the actual path to your folder

**Method E: Firefox (Most Permissive)**
1. Open Firefox
2. Type `about:config` in the address bar
3. Click "Accept the Risk and Continue"
4. Search for: `security.fileuri.strict_origin_policy`
5. Toggle it to `false`
6. Now you can open `index.html` by dragging it into Firefox
7. **Remember to toggle it back to `true` when done**

**For macOS/Linux users:** If assets don't load, start Chrome with:
```bash
# macOS
open -a "Google Chrome" --args --allow-file-access-from-files

# Linux
google-chrome --allow-file-access-from-files
```

### Option 3: Host Online

Upload the entire folder to:
- GitHub Pages (free)
- Netlify (free)
- Vercel (free)
- Any web hosting service

## Folder Structure

```
final_project_website/
├── index.html          # Main website file
├── assets/
│   ├── IMG_1047.mp4    # Demo video
│   ├── wave_sender.cpp
│   ├── img/            # Images for the site
│   └── UDP/            # Source code files
├── css/
│   └── styles.css      # All styling
└── js/
    └── scripts.js      # Interactive features
```

## Browser Compatibility

✅ Chrome (Windows, macOS, Linux)  
✅ Edge (Windows 11)  
✅ Firefox (Windows, macOS, Linux)  
✅ Safari (macOS, iOS)

## Sharing This Project

Simply zip the entire `final_project_website` folder and share it. The recipient can extract and follow the viewing instructions above.

**Note:** All assets use relative paths, so the folder structure must remain intact.

## Troubleshooting

**Video won't play:**
- Ensure `IMG_1047.mp4` exists in the `assets/` folder
- Use a local web server instead of opening the file directly
- Check that the video codec is H.264 (most compatible)

**Assets/Images not loading:**
- Use a local web server (Option 1 above)
- Don't move files outside their folders
- Check browser console (F12) for errors

**Bootstrap features not working:**
- Ensure you have an internet connection (for CDN resources)
- Check browser console for JavaScript errors
- Clear browser cache and reload
