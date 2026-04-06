import csv

# Tiled Flip Flags
FLIP_H = 0x80000000
FLIP_V = 0x40000000

# GBA Hardware Flip Flags
GBA_FLIP_H = 1 << 10
GBA_FLIP_V = 1 << 11

with open('townmap.csv', 'r') as csv_file:
    reader = csv.reader(csv_file)
    
    with open('townmap.h', 'w') as h_file:
        h_file.write('#define MAP_W 64\n')
        h_file.write('#define MAP_H 64\n\n')
        h_file.write('const unsigned short townMap[MAP_H][MAP_W] = {\n')
        
        for row in reader:
            if not row: continue
            
            gba_row = []
            for val in row:
                tiled_id = int(val)
                
                # 1. Check if Tiled flipped the tile
                is_flipped_h = bool(tiled_id & FLIP_H)
                is_flipped_v = bool(tiled_id & FLIP_V)
                
                # 2. Strip Tiled's extra data to get the raw tile number
                raw_id = tiled_id & 0x0FFFFFFF
                
                # 3. Apply the 1-Index fix we used to do in C
                gba_id = raw_id if raw_id > 0 else 0
                
                # 4. Add the GBA hardware flip flags
                if is_flipped_h: gba_id |= GBA_FLIP_H
                if is_flipped_v: gba_id |= GBA_FLIP_V
                
                gba_row.append(str(gba_id))
            
            h_file.write('    {' + ', '.join(gba_row) + '},\n')
            
        h_file.write('};\n')

print("townmap.h successfully created with GBA hardware flip flags!")