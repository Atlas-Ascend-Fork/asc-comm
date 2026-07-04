rdma_shared_provider(hns libhns.map
  # See Documentation/versioning.md
  1 1.0.${PACKAGE_VERSION}
  hns_roce_u.c
  hns_roce_u_buf.c
  hns_roce_u_db.c
  hns_roce_u_hw_v2.c
  hns_roce_u_verbs.c
  hns_roce_u_ai.c
)

target_link_libraries(hns LINK_PRIVATE ${CMAKE_DL_LIBS})
