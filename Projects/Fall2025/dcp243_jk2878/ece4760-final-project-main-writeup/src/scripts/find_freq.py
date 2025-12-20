fdict = dict()

dfreq = 25.175

min = float('inf')
min_rem = float('inf')
for freq in range(250, 400, 1):
  div = freq / dfreq
  
  ipart = int(div)
  fpart = round((div - ipart) * 256)
  
  act = freq / (ipart + (fpart/256))
  diff = abs(dfreq - act)
  
  fdict[freq] = {"diff":diff, "ipart":ipart, "fpart":fpart}
  
  if diff < min_rem:
    min_rem = diff
    min = freq
    
    
print(f"Found frequency: {min} with remainder {min_rem} .")
print()

freq_list = fdict.keys()

freq_list = sorted(freq_list, key=lambda k: fdict[k]["diff"])

for i, freq in enumerate(freq_list[0:10]):
  print(f"{i+1}:\t{freq}MHz\t[div = {fdict[freq]["ipart"]} + {fdict[freq]["fpart"]} / 256]\t(remainder {fdict[freq]["diff"]})")
