package org.nexoretro.app;

import android.os.Bundle;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

/**
 * La fenêtre de Nexo Retro sous Android.
 *
 * Tout le travail est fait par SDLActivity (la classe de SDL) : elle crée la
 * surface de dessin, transmet l'écran tactile, les manettes et le son, puis
 * charge libSDL3.so et libmain.so (notre C++) et appelle leur main().
 */
public class NexoActivity extends SDLActivity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // Pendant une partie, l'écran ne doit pas se mettre en veille.
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    }

    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL3", "main"};
    }
}
