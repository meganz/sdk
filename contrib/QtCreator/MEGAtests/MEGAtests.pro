TEMPLATE = subdirs

SUBDIRS += MEGAtest_unit
SUBDIRS += MEGAtest_integration
SUBDIRS += MEGAtool_purge_account

macx {
    SUBDIRS += MEGAtest_integration_fsevents_loader
    MEGAtest_integration_fsevents_loader.depends = MEGAtest_integration
}
