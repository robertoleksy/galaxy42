��          D      l       �   J   �   
   �      �      �   �    F   �  I  �  �   .  g   �                          Can not find script library $lib (dir_base_of_source=$dir_base_of_source). L_what_now L_what_now_about_compiled error_init_platforminfo Project-Id-Version: galaxy 42
Report-Msgid-Bugs-To: 
POT-Creation-Date: 2017-02-02 17:18-0500
PO-Revision-Date: 2016-08-19 17:23+0000
Last-Translator:  <info@yedino.com>
Language-Team: Polish
Language: pl
MIME-Version: 1.0
Content-Type: text/plain; charset=UTF8
Content-Transfer-Encoding: 8bit
Plural-Forms: nplurals=3; plural=(n==1 ? 0 : n%10>=2 && n%10<=4 && (n%100<10 || n%100>=20) ? 1 : 2);
 Nie można znaleźć biblioteki $lib (w katalogu $dir_base_of_source). Aby normalnie skompilować program natywnie (i go potem używać) proponujemy komendę:
  ./do 
A do budowy w Gitian (na ten system lub inne systemy) polecamy:
  ./build-gitian
Możesz zmienić opcje instalacji uruchamiając ./install.sh. Spis pozostałych możliwości zobaczysz uruchamiając ./menu lub czytając dokumentację. Teraz gdy program jest skompilowany i gotowy, uruchom go np poleceniem typu:
./tunserver.elf 
./tunserver.elf.exe 
Lub podobnym zależnie na jaką platformę go budowałeś. Nie udało się rozpoznać informacji o tym systemie operacyjnym/platformie które są tutaj używane. 