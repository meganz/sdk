target_sources(SDKlib PRIVATE
                      avl_tree.h
                      avl_tree_forward.h
                      avl_tree_iterator.h
                      avl_tree_node.h
                      avl_tree_traits.h
                      database_builder.h
                      file_context.h
                      file_context_badge.h
                      file_context_badge_forward.h
                      file_info_context.h
                      file_info_context_badge.h
                      file_info_context_badge_forward.h
                      file_service_context.h
                      file_service_queries.h
                      file_storage.h
                      from_file_id_map.h
                      logger.h
                      logging.h
                      type_traits.h
)

add_subdirectory(testing)

