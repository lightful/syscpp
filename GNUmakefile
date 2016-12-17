PROJECTS = examples/ActorThread/HelloWorld \
           examples/ActorThread/MyLibClient \
           examples/ActorThread/Test

export MK_FULLPATH = 1
export MK_NOHL = 1

project_build:
	@echo
	@for prj in $(PROJECTS); do \
	    echo ">>>>>> $$prj:" \
	    && $(MAKE) -C "$$prj" --no-print-directory $(MAKECMDGOALS) || exit \
	    && echo \
	; done

.PHONY: project_build $(MAKECMDGOALS)

$(foreach target, $(MAKECMDGOALS), $(eval $(target): project_build))
