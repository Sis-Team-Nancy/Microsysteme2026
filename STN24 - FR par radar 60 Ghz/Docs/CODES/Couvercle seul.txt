$fn = 80;

// -------- CHOIX DE LA PIECE --------
part = "lid";   // UNIQUEMENT "lid"

// -------- BOITIER (mêmes dimensions que la base) --------
box_L = 100;         // identique à la base
box_W = 65;
corner_r = 9;
wall     = 2.2;

// décalage identique à la base
offset_x = 12.5;

// -------- COUVERCLE --------
lid_t = 2.6;
clearance = 0.8;
lip_h = 3.2;
lip_t = 1.0;

// -------- FENÊTRE RADAR C1001 (AU-DESSUS DU CAPTEUR) --------
sensor_win_L = 22;      // 22 mm en X (largeur)
sensor_win_W = 22;      // 22 mm en Y (profondeur)
sensor_win_x = -25;     // position X (gauche, au-dessus C1001)
sensor_win_y = 0;       // centre Y

// -------- OUTILS --------
module rr2d(L, W, r){
  r2 = min(r, min(L,W)/2 - 0.01);
  hull(){
    translate([ L/2 - r2,  W/2 - r2]) circle(r=r2);
    translate([-L/2 + r2,  W/2 - r2]) circle(r=r2);
    translate([ L/2 - r2, -W/2 + r2]) circle(r=r2);
    translate([-L/2 + r2, -W/2 + r2]) circle(r=r2);
  }
}

module rr3d(L, W, H, r){
  linear_extrude(height=H) rr2d(L,W,r);
}

// -------- COUVERCLE COMPLET AVEC FENÊTRE --------
module lid_full(){
  outer_L = box_L - 2*(wall + clearance);
  outer_W = box_W - 2*(wall + clearance);
  inner_L = box_L - 2*(wall + clearance + lip_t);
  inner_W = box_W - 2*(wall + clearance + lip_t);

  difference(){
    union(){
      // Partie plate supérieure
      rr3d(box_L, box_W, lid_t, corner_r);

      // Rebord d'emboîtement
      translate([0,0,lid_t])
        difference(){
          rr3d(outer_L, outer_W, lip_h,
               max(corner_r-(wall+clearance),1));

          translate([0,0,-0.05])
            rr3d(inner_L, inner_W, lip_h+0.1,
                 max(corner_r-(wall+clearance+lip_t),1));
        }
    }

    // ========================================
    // FENÊTRE POUR RADAR C1001 (au centre-sup)
    // ========================================
    translate([sensor_win_x, sensor_win_y, lid_t/2])
      cube([sensor_win_L, sensor_win_W, lid_t+1], center=true);
  }
}

// -------- COUVERCLE DÉCALÉ --------
module lid_full_shifted(){
  translate([offset_x,0,0])
    lid_full();
}

// -------- RENDU FINAL --------
lid_full_shifted();
