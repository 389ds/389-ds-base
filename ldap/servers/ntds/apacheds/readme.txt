basic instructions for our apacheds:

for our jar component
# svn co http://svn.apache.org/repos/asf/directory
# cd directory/apacheds/trunk/
# apply the patch
# maven -D maven.test.skip=true multiproject:install
# cd main/target
# copy apacheds-main-${VER}.jar into component directory

for our source component:
# svn co http://svn.apache.org/repos/asf/directory
# cd directory/apacheds/
# cp -R trunk apacheds-${VER}
# zip apacheds-${VER} into apacheds-${VER}-src.zip
# copy apacheds-${VER}-src.zip into component directory


