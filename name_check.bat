@echo off

echo -------
echo -------

set Wildcard=*.h *.cpp *.inl *.c

echo TODOS FOUND:
findstr -s -n -i -l "ChangeEntityLocation" %Wildcard%

echo -------
echo -------
