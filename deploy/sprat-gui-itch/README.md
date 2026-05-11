# SPRAT GUI - WebAssembly Build

This is the WebAssembly build of SPRAT GUI for web deployment.

## Files

- `index.html` - Main entry point
- `sprat-gui.js` - WASM JavaScript loader
- `sprat-gui.data` - Application data
- `sprat-gui.wasm` - Uncompressed WASM binary
- `sprat-gui.wasm.gz` - Compressed WASM binary
- `qtloader.js` - Qt/WASM runtime loader
- `qtlogo.svg` - Qt logo for splash screen

## Browser Support

Requires a modern browser with WebAssembly support:
- Chrome/Edge 57+
- Firefox 52+
- Safari 11+

## Usage

Simply open `index.html` in a web browser to run the application.

### Compression

The `sprat-gui.wasm.gz` file is a gzip-compressed version of the WASM binary (reduced from 30MB to 8.4MB). Web servers can automatically serve this when the client supports gzip encoding.

## itch.io Deployment

To deploy this to itch.io:

1. Upload the contents of this directory to a new HTML5 game on itch.io
2. Set the game type to "HTML5"
3. Upload and publish

The game will run directly in the browser on itch.io's hosting.

