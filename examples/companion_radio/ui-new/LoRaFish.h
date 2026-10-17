#pragma once

// LoRaGotchi DX fish for Meck Watch (MECK_LORAFISH builds). See LoRaFish.cpp.

#ifdef MECK_LORAFISH

class UITask;
class LGFXDisplay;
struct NodePrefs;
namespace mesh { class RTCClock; }

// Results of a gesture on the fish screen.
enum {
  LORAFISH_MENU_STAY = 0,   // stay on the fish screen (redraw)
  LORAFISH_MENU_EXIT,       // back to the Games menu
  LORAFISH_MENU_HOME        // to the aquarium home page
};

void loRaFishBegin(UITask* task, NodePrefs* prefs, mesh::RTCClock* rtc);
void loRaFishLoop(bool aquariumOnScreen);

// Mesh events that feed the fish.
void loRaFishOnMessage();        // a text message arrived
void loRaFishOnContactAdvert();  // an advert from a contact was heard
void loRaFishOnAck();            // UI ack event (for example, advert sent)

// Draw straight to the panel; return milliseconds until the next frame.
int  loRaFishRenderAquarium(LGFXDisplay& d, const char* alertText);
int  loRaFishRenderMenu(LGFXDisplay& d, const char* alertText);

// Long press on the aquarium page. Returns true when the fish screen should
// open (to choose a new fish's colours).
bool loRaFishAquariumHold();

// Fish screen (opened from the Games menu).
void loRaFishOpenMenu();
int  loRaFishMenuTap(int vx, int vy);   // Meck 128x128 virtual coordinates
int  loRaFishMenuSwipe(int dx, int dy);
int  loRaFishMenuHold();
int  loRaFishMenuClick();               // side button

#endif
