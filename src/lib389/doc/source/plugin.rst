Plugin
==========

You can take plugin constant names here - https://github.com/389ds/389-ds-base/blob/master/src/lib389/lib389/_constants.py#L179

Usage example
--------------
::

    # Plugin and Plugins additionaly have 'enable', 'disable' and 'status' methods
    # Here I show you basic way to work with it. Additional methods of complex plugins will be described in subchapters

    from lib389.plugin import Plugins, ACLPlugin
    from lib389._constants import PLUGIN_ACL

    # You can just enable/disable plugins from Plugins interface
    plugins = Plugins(standalone)
    plugins.enable(PLUGIN_ACL)

    # Or you can first 'get' it and then work with it (make sense if your plugin is a complex one)
    aclplugin = ACLPlugin(standalone)

    aclplugin.enable()

    aclplugin.disable()

    # True if nsslapd-pluginEnabled is 'on', False otherwise - change the name?
    assert(uniqplugin.status())


Module documentation
-----------------------

.. automodule:: lib389.plugins
   :members:
