Goal of this fork to add dmabuf support
-------------------
Most of the code made by w23 (https://github.com/w23/) , scaledteam fixed bug with frame drops, fixed losting capture after switching between fullscreen windows and ported it into latest OBS Studio.

Important notes while building
-------------------
Since dmabuf (kmsgrab) capture require root to work, to make it usable from user account you need to add special caps to it:

``sudo setcap cap_sys_admin+ep  /usr/local/bin/obs``

While building you can encounter strange problem, 

  CMake Warning at CMakeLists.txt:15 (project):
  VERSION keyword not followed by a value or was followed by a value that
  expanded to nothing.

To fix it you need to add version number manually
``cmake .. -DOBS_VERSION_OVERRIDE=28.1.0-kmsgrab``

My personal setup
-------------------
I use Debian Testing (2022-11-04) with Gnome and X11, obs-studio compiled with this parameters:

``cmake .. -DENABLE_WEBSOCKET=OFF -DENABLE_VST=OFF -DENABLEVLC=OFF -DENABLE_BROWSER=OFF -DENABLE_NEW_MPEGTS_OUTPUT=OFF -DENABLE_AJA=OFF -DOBS_VERSION_OVERRIDE=28.1.0-kmsgrab``


OBS Studio <https://obsproject.com>
===================================

.. image:: https://github.com/obsproject/obs-studio/actions/workflows/main.yml/badge.svg?branch=master&event=push
   :alt: OBS Studio Build Status - GitHub Actions
   :target: https://github.com/obsproject/obs-studio/actions/workflows/main.yml?query=event%3Apush+branch%3Amaster

.. image:: https://badges.crowdin.net/obs-studio/localized.svg
   :alt: OBS Studio Translation Project Progress
   :target: https://crowdin.com/project/obs-studio

.. image:: https://img.shields.io/discord/348973006581923840.svg?label=&logo=discord&logoColor=ffffff&color=7389D8&labelColor=6A7EC2
   :alt: OBS Studio Discord Server
   :target: https://obsproject.com/discord

What is OBS Studio?
-------------------

OBS Studio is software designed for capturing, compositing, encoding,
recording, and streaming video content, efficiently.

It's distributed under the GNU General Public License v2 (or any later
version) - see the accompanying COPYING file for more details.

Quick Links
-----------

- Website: https://obsproject.com

- Help/Documentation/Guides: https://github.com/obsproject/obs-studio/wiki

- Forums: https://obsproject.com/forum/

- Build Instructions: https://github.com/obsproject/obs-studio/wiki/Install-Instructions

- Developer/API Documentation: https://obsproject.com/docs

- Donating/backing/sponsoring: https://obsproject.com/contribute

- Bug Tracker: https://github.com/obsproject/obs-studio/issues

Contributing
------------

- If you would like to help fund or sponsor the project, you can do so
  via `Patreon <https://www.patreon.com/obsproject>`_, `OpenCollective
  <https://opencollective.com/obsproject>`_, or `PayPal
  <https://www.paypal.me/obsproject>`_.  See our `contribute page
  <https://obsproject.com/contribute>`_ for more information.

- If you wish to contribute code to the project, please make sure to
  read the coding and commit guidelines:
  https://github.com/obsproject/obs-studio/blob/master/CONTRIBUTING.rst

- Developer/API documentation can be found here:
  https://obsproject.com/docs

- If you wish to contribute translations, do not submit pull requests.
  Instead, please use Crowdin.  For more information read this page:
  https://obsproject.com/wiki/How-To-Contribute-Translations-For-OBS

- Other ways to contribute are by helping people out with support on
  our forums or in our community chat.  Please limit support to topics
  you fully understand -- bad advice is worse than no advice.  When it
  comes to something that you don't fully know or understand, please
  defer to the official help or official channels.
