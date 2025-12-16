# Hello.
This is ChucklesOS, a hobby OS developed by Github user generalchuckles-cm
The goal of this OS is to make a minimal Linux like experience
Most features work fine

It makes use of the Limine bootloader.

Current Functions (12.14.25):
Full SATA/AHCI Support
FAT32 Filesystem
Intel Gemini Lake GPU driver (not super functional lmao)
Intel xHCI driver for Gemini Lake (It works but I can't get it to work on my Celeron J4125. Up for testing!)
Bouncing DVD Logo
Stress Test (relies on intel GPU driver, will not work lol)
Basic 3D renderer
NES Emulator (Only Mapper 0 games will work. Mapper 4 games half work but are very buggy)
    ### Note that in VMs like QEMU, the NES timing is off. On real hardware
    ### It plays at intended speed.
Window Manager with Windows 98 SE Theme
PS/2 Keyboard/Mouse support
MIDI Player (Not really MIDI, but an included tool to convert a MIDI file to H is included. Uses the PC Speaker)


Current Functions (12.16.25):
NES and Text Editor are windowed
Theme editor
Display options
SMP Support (Theoreticlly up to 32 Cores)
System Stats



# How to contribute:

1. Click the "Fork" button at the top-right of the chucklesOS repository page.
2. This will create your own copy of chucklesOS under your GitHub account.
3. Clone your fork to your local machine:
   git clone https://github.com/generalchuckles-cm/chucklesOS.git
4. Navigate into the project folder:
   cd chucklesOS
5. Add the main repository as an upstream remote:
   git remote add upstream https://github.com/generalchuckles-cm/chucklesOS.git
6. Fetch upstream changes whenever needed:
   git fetch upstream
7. Merge upstream changes into your local branch:
   git merge upstream/main
8. Always create a new branch for each feature or bug fix:
   git checkout -b feature-or-bugfix-name
9. Make your changes in your branch.
10. Test thoroughly in QEMU or on your hardware to ensure nothing breaks.
11. Keep commits small and descriptive.
12. Stage changes:
    git add .
13. Commit changes:
    git commit -m "Short descriptive message about your change"
14. Push your branch to your fork:
    git push origin feature-or-bugfix-name
15. Go to your fork on GitHub.
16. Click "Compare & pull request".
17. Provide a descriptive title and details about your change.
18. Submit the pull request.
19. Your PR will be reviewed. Make requested changes by pushing commits to the same branch.
20. Once approved, it will be merged into the main repository.
21. Regularly sync your fork with upstream to avoid conflicts:
    git fetch upstream
    git checkout main
    git merge upstream/main
    git push origin main
