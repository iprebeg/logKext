#!/bin/sh
sudo launchctl unload /Library/LaunchDaemons/logKext.plist
sudo rm -f /Library/LaunchDaemons/logKext.plist
sudo rm -rf /System/Library/Extensions/logKext.kext
sudo rm -f /System/Library/Filesystems/logKextDaemon
sudo rm -f /Library/Preferences/logKextKeymap.plist
sudo rm -rf /Library/Receipts/logKext.pkg
if [ -n "`sudo defaults read com.fsb.logKext Pathname | grep 'does not exist'`" ];
then
sudo rm '`sudo defaults read com.fsb.logKext Pathname`'
fi;
sudo rm -f /usr/bin/logKextClient
sudo rm -f /LogKext\ Readme.html
sudo defaults delete com.fsb.logKext
sudo rm -f /LogKextUninstall.command
sudo kextunload -b com.fsb.kext.logKext
