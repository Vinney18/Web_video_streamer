// move-files.js

const fs = require('fs');
const path = require('path');

// Define source and destination directories
const sourceDir = // current directory
    path.join(__dirname, '');
const destinationDir = '../../PlayerServer/PlayerServer/wwwroot/player';

// Define files to move
const filesToCopy = ['index.html', 'build/i2v_player.min.js', 'build/i2v-player.js', 'moment.min.js', 'jmuxer.js'];
const filestoCopyTo = ['index.html', 'i2v_player.min.js', 'i2v-player.js', 'moment.min.js', 'jmuxer.js'];
// Create destination directory if it doesn't exist
if (!fs.existsSync(destinationDir)) {
  fs.mkdirSync(destinationDir, { recursive: true });
}

// Move files
filesToCopy.forEach(file => {
  const sourcePath = // current directory
    path.join(sourceDir, file);
    const destinationPath = path.join(destinationDir, filestoCopyTo[filesToCopy.indexOf(file)]);
    fs.copyFileSync(sourcePath, destinationPath);
    console.log(`Copied ${file} to ${destinationPath}`);
});
