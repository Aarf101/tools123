#include <iostream>
#include <unistd.h>
#include <pthread.h>
#include <string.h> 
#include <libtorrent/session.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/torrent_status.hpp>
#include "ui.h" 

namespace lt = libtorrent;

typedef struct {
    lt::session *ses;
    lt::torrent_handle h;
    double progress;
    uiProgressBar *pbar;
    uiLabel *statusLabel;
    uiEntry *urlEntry;
    uiButton *pauseBtn;
    bool isPaused;
} App;

void *background_download(void *data);


void setFinishedLabel(void *data) {
    App *ctx = (App *)data;
    uiLabelSetText(ctx->statusLabel, "Download Complete / Seeding");
}

void updateUI(void *data) {
    App *ctx = (App *)data;
    uiProgressBarSetValue(ctx->pbar, (int)ctx->progress);
    
    char buf[64];
    snprintf(buf, sizeof(buf), "Progress: %.2f%%", ctx->progress);
    uiLabelSetText(ctx->statusLabel, buf);
}

void onStartClicked(uiButton *b, void *data) {
    App *ctx = (App *)data;
    if (ctx->ses != nullptr) return; 

    char *inputUrl = uiEntryText(ctx->urlEntry);
    if (strlen(inputUrl) == 0) {
        uiFreeText(inputUrl);
        uiLabelSetText(ctx->statusLabel, "Please paste a magnet link first");
        return;
    }

    lt::settings_pack pack;
    pack.set_str(lt::settings_pack::user_agent, "badtorrent/1.0");
    ctx->ses = new lt::session(pack);

    try {
        lt::add_torrent_params p = lt::parse_magnet_uri(inputUrl);
        uiFreeText(inputUrl);
        
        //  user home directory
        const char *home_dir = getenv("HOME");
        std::string save_path;
            
        if (home_dir != nullptr) {
            //  point to the global Downloads folder
            save_path = std::string(home_dir) + "/Downloads";
        } else {
            // Fallback 
            save_path = "./Downloads"; 
        }
            
        p.save_path = save_path;
        
        ctx->h = ctx->ses->add_torrent(p);
        
        pthread_t thread_id;
        pthread_create(&thread_id, NULL, background_download, ctx);
        
        uiControlDisable(uiControl(b));
        uiControlDisable(uiControl(ctx->urlEntry)); // Disable input once started
        uiControlEnable(uiControl(ctx->pauseBtn));
        uiLabelSetText(ctx->statusLabel, "Starting...");
    } catch (std::exception const& e) {
        uiFreeText(inputUrl);
        uiLabelSetText(ctx->statusLabel, "Error: Invalid magnet link");
    }
}

void onPauseClicked(uiButton *b, void *data) {
    App *ctx = (App *)data;
    
    // don't do anything if the download hasn't started
    if (!ctx->h.is_valid()) return; 

    if (ctx->isPaused) {
        ctx->h.resume();
        uiButtonSetText(b, "Pause");
        uiLabelSetText(ctx->statusLabel, "Resuming...");
        ctx->isPaused = false;
    } else {
        ctx->h.pause();
        uiButtonSetText(b, "Resume");
        uiLabelSetText(ctx->statusLabel, "Paused.");
        ctx->isPaused = true;
    }
}

void *background_download(void *data) {
    App *ctx = (App *)data;
    while (true) {
        if (!ctx->h.is_valid()) break;

        lt::torrent_status s = ctx->h.status();
        ctx->progress = s.progress * 100.0;
        
        uiQueueMain(updateUI, ctx);
        
        if (s.is_seeding) {
            uiQueueMain(setFinishedLabel, ctx);
            break;
        }
        usleep(500000); 
    }
    return NULL;
}

int onClosing(uiWindow *w, void *data) {
    uiQuit();
    return 1;
}

int main(void) {
    uiInitOptions o = {0}; 
    const char *err = uiInit(&o);
    if (err != NULL) {
        uiFreeInitError(err);
        return 1;
    }


    App ctx;
    ctx.ses = nullptr;
    ctx.progress = 0;

    uiWindow *w = uiNewWindow("badtorrent", 400, 200, 0);
    uiWindowSetMargined(w, 1);
    uiWindowOnClosing(w, onClosing, NULL);

    uiBox *vbox = uiNewVerticalBox();
    uiBoxSetPadded(vbox, 1);
    uiWindowSetChild(w, uiControl(vbox));

    uiBoxAppend(vbox, uiControl(uiNewLabel("Paste Magnet Link:")), 0);
    ctx.urlEntry = uiNewEntry();
    uiEntrySetText(ctx.urlEntry, ""); 
    uiBoxAppend(vbox, uiControl(ctx.urlEntry), 0);

    ctx.pbar = uiNewProgressBar();
    uiBoxAppend(vbox, uiControl(ctx.pbar), 0);

    ctx.statusLabel = uiNewLabel("Ready");
    uiBoxAppend(vbox, uiControl(ctx.statusLabel), 0);

    uiButton *btn = uiNewButton("Start Download");
    uiButtonOnClicked(btn, onStartClicked, &ctx);
    uiBoxAppend(vbox, uiControl(btn), 0);

    ctx.pauseBtn = uiNewButton("Pause");
    uiButtonOnClicked(ctx.pauseBtn, onPauseClicked, &ctx);
    uiControlDisable(uiControl(ctx.pauseBtn)); 
    uiBoxAppend(vbox, uiControl(ctx.pauseBtn), 0);

    uiControlShow(uiControl(w));
    uiMain();

    // Clean up 
    if (ctx.ses) delete ctx.ses;

    uiUninit();
    return 0;
}