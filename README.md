# MotionExtraction

This program takes in an video and creates an interesting visual effect that shows how objects in the video change over time.
The program was written in C using the ffmpeg libraries.
This amazing effect is based on [this youtube video](https://www.youtube.com/watch?v=NSS6yAMZF78 'Motion Extraction') of the same name by Posy.

### Usage

./MotionExtraction  input-file  output-file

### Example

Below is a still frame from the original video.

<img width="1274" alt="Screenshot 2024-01-26 at 10 37 37 AM" src="https://github.com/henryrossi/MotionExtraction/assets/102625896/b4ad2d27-b9ac-40f2-92e6-5ad2c7e750dc">

Now the same frame from the new video shows everything that has changed since the first frame. Here the only thing that has changed is the position of the swing. You can see its current position outlined in black and the swing's original position outlined in white.

<img width="1274" alt="Screenshot 2024-01-26 at 10 37 00 AM" src="https://github.com/henryrossi/MotionExtraction/assets/102625896/45194327-f7d5-44fd-90a2-fe8e2db9ce4d">

Very minor changes can also be seen through the faint outlines of the swing on the left and the tree on the right.

This cool trick requires the video footage to be stable since we want unmoving objects to be in the same position on every frame. Footage should be taken using a mount or stand of some kind.

If you want to see some more impressive examples and effects, check out [Posy's youtube video](https://www.youtube.com/watch?v=NSS6yAMZF78 'Motion Extraction') that inspired this project.
