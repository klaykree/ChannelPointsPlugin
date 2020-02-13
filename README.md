## Instructions
- Add both .dll files to the `obs-studio\obs-plugins\64bit` folder in your OBS install location
- Add a `Channel Points Display` source to your scene
- Properties:
  - Set the `Channel Name` to the same as your streaming channel name
  - Increase the number of redemptions if required (must click OK and re-open the properties to update the list)
  - Set the `Redemption Title` to the same as the redemption that will activate the image display
  - Set the `Media Folder` to the directory that will be used for the random media files
  - Modify the `Media Extensions` (semicolon delimited list), `Fade Duration`, and `Show Duration` as desired
- The plugin will automatically create an image source named `_cpd_image`. Do not rename this since the plugin relies on the name.
- Move and resize the `_cpd_image` as desired to have the image display in the correct location.
- Only create one `Channel Points Display` source, it will unlikely work with more than one.

### Special Thanks
- OBS for the streaming software
- Microsoft for the webview2 library
- The fantastic communities of streamers on Twitch that I've been lucky to find
