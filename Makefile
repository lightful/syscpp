PROJECTS = examples\ActorThread\HelloWorld \
           examples\ActorThread\MyLibClient \
           examples\ActorThread\Test

all:
	@echo off && for %%p in ($(PROJECTS)) do \
	    echo. && echo ^>^>^>^>^>^> %%p: \
	    && pushd %%p && $(MAKE) /NOLOGO && popd \
	    && echo.

clean:
	@echo off && for %%p in ($(PROJECTS)) do \
	    echo. && echo ^>^>^>^>^>^> %%p: \
	    && pushd %%p && $(MAKE) clean /NOLOGO && popd \
	    && echo.
