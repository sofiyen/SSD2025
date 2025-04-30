TMPDIR=awesome_rootkit_tmp
ZIPNAME=hw1.zip
ZIPFILES=test/ write-up.md src/Makefile src/rootkit.c src/rootkit.h README.md

all: zip

zip:
	mkdir -p $(TMPDIR)
	cp -r $(ZIPFILES) $(TMPDIR)
	cd $(TMPDIR) && zip -r ../$(ZIPNAME) .
	rm -rf $(TMPDIR)

clean:
	rm -f $(ZIPNAME)