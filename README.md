# dockerPatcher
Patcher for Docker on Windows 10 1903 issues

It turns out there is a pretty debilitating bug on Windows 10 1903 (workstation, not server OS) that kind of makes docker on this version of Windows intermittantly not work.

https://github.com/docker/for-win/issues/3884

The symptom is getting this error on some machines when attempting to build Docker images: hcsshim::PrepareLayer - failed failed in Win32: Incorrect function. (0x1)

This happens on some 1903 machines, and not at all on others - I have not been able to figure out why it happens but someone (https://github.com/giniyat202 huge shoutout to that guy) figured out that by changing some of the parameters being used in a vmcompute.dll call, the problem disappears.

After a Windows update the vmcompute.dll binary minorly changed which is why the code this patcher uses is slightly different from the original solution posted by giniyat202

I automate this patcher in my team's development environment automation solution, if the issue hits someone's machine they can run the patcher and it will build images as needed - this only needs to be used for building images, once the images are built Docker or the machine can be restarted without causing the issue to resurface.
