TMPDIR=awesome_userfault_tmp
ZIPNAME=hw2.zip
ZIPFILES=test/ write-up.md syscall.patch README.md

all: zip

zip:
	mkdir -p $(TMPDIR)
	cp userfault/* $(TMPDIR)
	cp -r $(ZIPFILES) $(TMPDIR)
	cd $(TMPDIR) && zip -r ../$(ZIPNAME) .
	rm -rf $(TMPDIR)

clean:
	rm -f $(ZIPNAME)